import AppKit
import QuartzCore
import UniformTypeIdentifiers

/// Archive (spec §3.9, §3.27): the category column and document grid, the
/// document editor with the format bar and media, the classified flow and
/// in-place category rename. ArchiveController owns the data and the
/// classification; this canvas only presents and edits them.
final class ArchiveCanvas: HUDDesignCanvas, NSTextViewDelegate {
    enum Filter: Equatable { case all, uncategorized, category(UUID) }
    enum Page: Equatable { case list, editor(UUID), locked(UUID) }

    let controller: ArchiveController
    var shelfChoices: (() -> [NotesShelfMediaChoice])?
    var shelfAccess: ((UUID) throws -> ShelfFileAccess)?
    /// An open panel or the owner-authentication prompt: the HUD stays open.
    override var isPresentingPanel: Bool { panel != nil || recovering }
    private(set) var filter: Filter = .all
    private(set) var mediaIndex = 0
    private(set) var presentation: NotesMediaPresentation?

    // MARK: Geometry (content-box px)

    static let columnWidth: CGFloat = 150
    static let columnHeight: CGFloat = 420
    static let plateGap: CGFloat = 6
    static let addRect = CGRect(x: 0, y: HUDMetric.headerHeight + columnHeight - 30, width: 30, height: 30)
    static let renameRect = addRect.offsetBy(dx: 36, dy: 0)
    static let removeCategoryRect = addRect.offsetBy(dx: 72, dy: 0)
    static let gridRect = CGRect(x: columnWidth + 12, y: HUDMetric.headerHeight, width: HUDMetric.box.width - columnWidth - 12, height: columnHeight)
    static let tileHeight: CGFloat = 96 + 24
    // The editor gives the body and the media the whole height: tight 6 px
    // gaps under the title and the bar, the footer on the box's bottom edge.
    static let titleRect = CGRect(x: 0, y: HUDMetric.headerHeight, width: HUDMetric.box.width, height: 36)
    static let barRect = CGRect(x: 0, y: titleRect.maxY + 6, width: HUDMetric.box.width, height: 26)
    static let footerY = HUDMetric.box.height - 26
    static let bodyRect = CGRect(x: 0, y: barRect.maxY + 6, width: HUDMetric.box.width, height: footerY - 8 - barRect.maxY - 6)
    static let mediaRect = CGRect(x: 0, y: bodyRect.maxY - 150, width: HUDMetric.box.width, height: 150)
    static let lockedRect = CGRect(x: 0, y: HUDMetric.headerHeight, width: HUDMetric.box.width, height: HUDMetric.box.height - HUDMetric.headerHeight)
    /// The body shrinks to make room for the media strip.
    var bodyRect: CGRect {
        var rect = Self.bodyRect
        if !(controller.selected?.media.isEmpty ?? true) { rect.size.height = Self.mediaRect.minY - 8 - rect.minY }
        return rect
    }

    // MARK: State

    /// What the current page's layers depend on; anything else changes in place.
    private struct Structure: Equatable {
        struct Category: Equatable { let id: UUID; let name: String; let isNew: Bool; let count: Int }
        struct Document: Equatable { let id: UUID; let number: Int; let title: String; let date: Date; let classified: Bool; let media: Int }
        var page: Page
        var language = L10n.resolvedLanguage
        var filter: Filter = .all
        var categories: [Category] = []
        var documents: [Document] = []
        var total = 0, uncategorized = 0
        var categoryName = ""
        var classification = ArchiveClassificationStatus.unclassified
        var media = 0, mediaIndex = 0, playing = false, mediaFailed = false
    }
    private enum Field { case title, body }
    private struct Focus { let document: UUID; let field: Field; let selection: NSRange }

    private var renderedStructure: Structure?
    private var needsRefresh = false
    /// Deactivated: changes wait for the next entrance instead of repainting the leaving page.
    private var leaving = false
    private var controllerActivated = false
    private var binders: [() -> Void] = []
    private var pendingOpen: UUID?
    private var unlocking: UUID?
    private var opening: UUID?
    private var openingWork: DispatchWorkItem?
    private var sealing: UUID?
    private var reportedError: String?
    private var focusMemory: Focus?
    private var edited = false
    private var recovering = false
    private var mediaRefreshQueued = false
    /// Set while a change is made just before a sub-navigation builds its page.
    private var holdingRefresh = false
    /// Category list offset kept across the vertical category switch.
    private var keptCategoryOffset: CGFloat?
    private static var owners: [ObjectIdentifier: ObjectIdentifier] = [:]

    // List page
    private weak var categoryArea: HUDScrollArea?
    private weak var gridArea: HUDScrollArea?
    private var plates: [UUID: ArchiveCategoryPlate] = [:]
    private var shownDocuments: [ArchiveSummary] = []
    private var tiles: [UUID: HUDArchiveTile] = [:]
    private var tileWidth: CGFloat = 132
    private var rename: (id: UUID, field: HUDDesignTextField)?
    private let thumbnails = ArchiveGalleryThumbnails()
    private var thumbnailReferences: [UUID: NotesMediaReference] = [:]
    private var metadataRequests: Set<UUID> = []
    private var thumbnailGeneration = 0

    // Editor page: the title field and body survive in-place re-renders of one document.
    private var documentID: UUID?
    private var titleField: HUDDesignTextField?
    private var bodyArea: ArchiveBodyArea?
    private var formatBar: NotesFormatBar?
    private weak var progressFill: CALayer?
    private var presentationKey: String?
    private var panel: NSOpenPanel?
    private var importTicket = ArchiveImportTicket()
    private let importQueue = DispatchQueue(label: "EndfieldHUD.ArchiveImport", qos: .utility)

    // Locked page and the password pop-overs
    private var lockedPanel: ArchiveLockedPanel?
    private var passwordField: HUDDesignTextField?
    private weak var unlockButton: HUDButton?
    private var popoverFields: [HUDDesignTextField] = []

    init(context: HUDDesignContext, controller: ArchiveController) {
        self.controller = controller
        super.init(module: .archive, context: context)
        // A document unlocked in an earlier HUD session opens locked again.
        controller.lockSelected()
        Self.owners[ObjectIdentifier(controller)] = ObjectIdentifier(self)
        controller.onChange = { [weak self] in self?.controllerChanged() }
        thumbnails.onChange = { [weak self] id, image in self?.tiles[id]?.setPoster(image) }
    }

    deinit {
        openingWork?.cancel(); importTicket.cancel(); panel?.cancel(nil)
        bodyArea?.dispose(); thumbnails.clear()
        presentation?.onStateChange = nil; presentation?.onProgress = nil; presentation?.dispose()
        // The last canvas of a controller closes its document (the HUD closed).
        if Self.owners[ObjectIdentifier(controller)] == ObjectIdentifier(self) {
            Self.owners[ObjectIdentifier(controller)] = nil
            controller.onChange = nil
            controller.deactivate()
        }
    }

    // MARK: Lifecycle

    override func makeContent(for module: HUDModule, style: HUDModuleContentStyle) -> CALayer {
        activateController()
        leaving = false
        // Bring the page up to date before the entrance plays on it.
        if renderedLanguage != nil, renderedLanguage == L10n.resolvedLanguage, needsRefresh || structure != renderedStructure { rerender() }
        let content = super.makeContent(for: module, style: style)
        bodyArea?.attach()
        return content
    }

    override func setActive(_ active: Bool) {
        if !active, isActive { focusMemory = currentFocus() }
        super.setActive(active)
    }

    override func prepareToLeave() {
        if let focus = currentFocus() { focusMemory = focus }
        super.prepareToLeave()
    }

    override func didActivate() {
        leaving = false
        activateController()
        if controller.error != nil { controller.retryPendingSaves() }
        if needsRefresh || structure != renderedStructure { refresh() }
        bodyArea?.attach()
        if let entry = controller.selected, case .editor = renderedStructure?.page { syncMedia(entry) }
        loadVisibleThumbnails()
        restoreFocus()
        focusPasswordSoon()
    }

    override func didDeactivate() {
        leaving = true
        finishOpening(announce: false)
        unlocking = nil
        bodyArea?.suspend()
        clearMedia()
        thumbnailGeneration += 1; metadataRequests.removeAll(); thumbnails.clear()
        panel?.cancel(nil); panel = nil; importTicket.cancel(); importTicket = ArchiveImportTicket()
        reportEdits()
        controller.flush()
        // Leaving the module relocks a classified document; its text leaves memory with the editor.
        if controller.selectedClassification != .unclassified, !controller.isSelectedLocked {
            discardDocument()
            controller.lockSelected()
        }
    }

    override func recordingModeChanged() {}

    private func activateController() {
        guard !controllerActivated else { return }
        controllerActivated = true
        controller.activate()
    }

    // MARK: Controller changes

    private func controllerChanged() {
        if let pending = pendingOpen {
            if controller.selected?.id == pending { pendingOpen = nil; mediaIndex = 0; navigate(dx: 1); return }
            if controller.error != nil { pendingOpen = nil }
        }
        reportError()
        refreshWhenPossible()
    }

    /// Re-renders when the structure changed, otherwise updates in place.
    /// Waits while the page is leaving, a menu or pop-over is open, or a
    /// category is being renamed.
    private func refreshWhenPossible() {
        guard renderedLanguage != nil else { return }
        if holdingRefresh || isTransitioning || leaving || rename != nil || isMenuOpen || isPopoverOpen { needsRefresh = true; return }
        refresh()
    }

    private func refresh() {
        needsRefresh = false
        if structure != renderedStructure { rerender() } else { binders.forEach { $0() } }
        focusPasswordSoon()
    }

    /// Changes the controller without repainting: the sub-navigation that
    /// follows leaves with the old page and builds the new one.
    private func holding(_ change: () -> Void) {
        holdingRefresh = true; change(); holdingRefresh = false
    }

    private func refreshSoon() {
        DispatchQueue.main.async { [weak self] in
            guard let self, self.needsRefresh else { return }
            self.refreshWhenPossible()
        }
    }

    private func reportError() {
        let error = controller.error
        defer { reportedError = error }
        guard let error, error != reportedError, isActive else { return }
        toast(icon: .warn, "Archive error", "档案库错误", detail: error)
    }

    private var currentPage: Page {
        guard let entry = controller.selected else { return .list }
        if controller.isSelectedLocked || unlocking == entry.id || opening == entry.id { return .locked(entry.id) }
        return .editor(entry.id)
    }

    private var structure: Structure {
        var value = Structure(page: currentPage)
        switch value.page {
        case .list:
            value.filter = filter
            let entries = controller.entries
            value.total = entries.count
            var counts: [UUID: Int] = [:]
            for entry in entries { if let id = entry.categoryID { counts[id, default: 0] += 1 } else { value.uncategorized += 1 } }
            value.categories = controller.categories.map {
                Structure.Category(id: $0.id, name: $0.name, isNew: $0.isNew, count: counts[$0.id] ?? 0)
            }
            let numbers = Dictionary(entries.enumerated().map { ($0.element.id, $0.offset + 1) }, uniquingKeysWith: { a, _ in a })
            value.documents = filteredDocuments.map {
                Structure.Document(id: $0.id, number: numbers[$0.id] ?? 0, title: $0.title, date: $0.date,
                                   classified: $0.isClassified, media: $0.isClassified ? 0 : $0.mediaCount)
            }
        case .editor, .locked:
            let entry = controller.selected
            value.categoryName = categoryName(entry?.categoryID)
            value.classification = controller.selectedClassification
            if case .editor = value.page, let entry {
                value.media = entry.media.count
                value.mediaIndex = min(mediaIndex, max(0, entry.media.count - 1))
                value.playing = presentation?.isPlaying ?? false
                value.mediaFailed = presentation?.error != nil
            }
        }
        return value
    }

    private var filteredDocuments: [ArchiveSummary] {
        let entries = controller.entries
        switch filter {
        case .all: return entries
        case .uncategorized: return entries.filter { $0.categoryID == nil }
        case .category(let id): return entries.filter { $0.categoryID == id }
        }
    }

    private func categoryName(_ id: UUID?) -> String {
        id.flatMap { id in controller.categories.first { $0.id == id }?.name } ?? L10n.text("Uncategorized", "未分类")
    }

    // MARK: Pages

    override func buildPage(_ page: HUDDesignPage) {
        binders = []; needsRefresh = false
        if case .category(let id) = filter, !controller.categories.contains(where: { $0.id == id }) { filter = .all }
        let current = structure
        renderedStructure = current
        lockedPanel = nil; passwordField = nil; formatBar = nil; progressFill = nil
        switch current.page {
        case .list:
            discardDocument(); clearMedia()
            buildList(page, current)
        case .editor:
            plates = [:]; tiles = [:]
            buildEditor(page)
        case .locked(let id):
            plates = [:]; tiles = [:]
            discardDocument(); clearMedia()
            buildLocked(page, id)
        }
        // Pop-over fields stay registered across in-place re-renders.
        popoverFields.forEach(register)
    }

    // MARK: List view (§3.9.1)

    private func buildList(_ page: HUDDesignPage, _ current: Structure) {
        plates = [:]; tiles = [:]
        let documents = filteredDocuments
        shownDocuments = documents
        let new = HUDButton(id: "new", title: L10n.text("New", "新建"), icon: .plus, kind: .primary, size: .small, env: env)
        new.place(at: .zero)
        new.onActivate = { [weak self] in self?.createDocument() }
        header.configure(crumbs: ["ARCHIVE"], code: "MOD-04", title: HUDModuleTitle("Archive", "档案库"),
                         counter: HUDFormat.pad2(documents.count) + " DOCS", showsBack: false, actions: [new])
        buildCategories(page, current)
        buildGrid(page, documents)
    }

    private func buildCategories(_ page: HUDDesignPage, _ current: Structure) {
        let top = HUDMetric.headerHeight
        let rows: [(Filter, String, Int, Bool)] = [(.all, L10n.text("All", "全部"), current.total, false),
                                                  (.uncategorized, L10n.text("Uncategorized", "未分类"), current.uncategorized, false)]
            + current.categories.map { (.category($0.id), $0.name, $0.count, $0.isNew) }
        let height = Self.columnHeight - 30 - Self.plateGap
        // 4 px beyond the column so the hover shift is never clipped.
        let area = page.scrollArea(id: "categories", frame: CGRect(x: 0, y: top, width: Self.columnWidth + 4, height: height))
        area.setContentHeight(CGFloat(rows.count) * (ArchiveCategoryPlate.height + Self.plateGap) - Self.plateGap)
        if let kept = keptCategoryOffset { area.setOffset(kept); keptCategoryOffset = nil } else { page.restoreOffset(of: area) }
        categoryArea = area
        let width = area.maximumOffset > 0 ? Self.columnWidth - 10 : Self.columnWidth
        for (index, row) in rows.enumerated() {
            let id: String
            switch row.0 {
            case .all: id = "category:all"
            case .uncategorized: id = "category:uncategorized"
            case .category(let uuid): id = "category:" + uuid.uuidString
            }
            let plate = ArchiveCategoryPlate(id: id, title: row.1, count: row.2, isNew: row.3, selected: row.0 == filter, width: width, env: env)
            plate.onActivate = { [weak self] in self?.selectFilter(row.0) }
            HUDMotion.perform {
                plate.layer.frame.origin = CGPoint(x: 0, y: CGFloat(index) * (ArchiveCategoryPlate.height + Self.plateGap))
                area.content.addSublayer(plate.layer)
            }
            page.add(plate)
            if plate.layer.frame.minY < area.offset + height, plate.layer.frame.maxY > area.offset { page.stagger(plate.layer) }
            if case .category(let uuid) = row.0 { plates[uuid] = plate }
        }
        let fixed: Bool = { if case .category = filter { return false }; return true }()
        let ops = HUDLayer.plain("archive.categoryOps")
        HUDMotion.perform { ops.frame = CGRect(x: 0, y: Self.addRect.minY, width: Self.columnWidth, height: 30); page.layer.addSublayer(ops) }
        for (icon, id, label) in [(HUDIcon.plus, "addCategory", L10n.text("Add category", "添加分类")),
                                  (.pen, "renameCategory", L10n.text("Rename category", "重命名分类")),
                                  (.minus, "removeCategory", L10n.text("Remove category", "删除分类"))] {
            let button = HUDIconButton(id: id, label: label, icon: icon, env: env)
            HUDMotion.perform {
                button.layer.frame = CGRect(x: CGFloat(ops.sublayers?.count ?? 0) * 36, y: 0, width: 30, height: 30)
                ops.addSublayer(button.layer)
            }
            button.isEnabled = id == "addCategory" || !fixed
            button.onActivate = { [weak self] in
                switch id {
                case "addCategory": self?.addCategory()
                case "renameCategory": self?.beginRename()
                default: self?.confirmRemoveCategory()
                }
            }
            page.add(button)
        }
        page.stagger(ops)
        env.applyScale(to: ops)
    }

    private func buildGrid(_ page: HUDDesignPage, _ documents: [ArchiveSummary]) {
        let area = page.scrollArea(id: "docs", frame: Self.gridRect)
        gridArea = area
        let rows = (documents.count + 2) / 3
        area.setContentHeight(documents.isEmpty ? 258 : 8 + CGFloat(rows) * Self.tileHeight + CGFloat(max(0, rows - 1)) * 12)
        page.restoreOffset(of: area)
        tileWidth = floor((area.contentWidth - 8 - 24) / 3)
        if documents.isEmpty {
            let empty = HUDEmptyState.make(title: "EMPTY", size: CGSize(width: area.contentWidth - 8, height: 250), env: env)
            HUDMotion.perform { empty.frame.origin = CGPoint(x: 4, y: 4); area.content.addSublayer(empty) }
            page.stagger(empty)
            return
        }
        layoutTiles(in: page, stagger: true)
        DispatchQueue.main.async { [weak self] in self?.loadVisibleThumbnails() }
    }

    /// Only tiles in (or touching) the viewport exist; scrolling adds and
    /// removes them, so a large archive keeps a small layer tree.
    private func layoutTiles(in page: HUDDesignPage, stagger: Bool = false) {
        guard let area = gridArea else { return }
        let pitch = Self.tileHeight + 12
        let first = max(0, Int(floor((area.offset - 4) / pitch))) * 3
        let last = min(shownDocuments.count, (Int(ceil((area.offset + area.layer.bounds.height - 4) / pitch)) + 1) * 3)
        let wanted = first < last ? Array(shownDocuments[first..<last]) : []
        let ids = Set(wanted.map(\.id))
        var changed = false
        for (id, tile) in tiles where !ids.contains(id) {
            HUDMotion.perform { tile.layer.removeFromSuperlayer() }
            page.remove(tile); tiles[id] = nil; changed = true
        }
        let numbers = Dictionary(controller.entries.enumerated().map { ($0.element.id, $0.offset + 1) }, uniquingKeysWith: { a, _ in a })
        for (offset, summary) in wanted.enumerated() where tiles[summary.id] == nil {
            let index = first + offset
            let date = HUDFormat.dotted(summary.date)
            let title = summary.isClassified ? "[ " + L10n.text("CLASSIFIED", "机密") + " ]"
                : (summary.title.isEmpty ? L10n.text("Untitled", "未命名") : summary.title)
            let tile = HUDArchiveTile(id: "doc:" + summary.id.uuidString, number: numbers[summary.id] ?? index + 1, title: title,
                                      ext: .txt, extLabel: "DOC", size: nil, date: date, width: tileWidth, wellHeight: 96,
                                      style: summary.isClassified ? .classified : .standard, env: env)
            tile.label = (summary.isClassified ? L10n.text("Classified document", "机密文档") : title) + ", " + date
            tile.onActivate = { [weak self] in self?.openDocument(summary.id) }
            if !summary.isClassified { tile.setPoster(thumbnails.image(for: summary.id, reference: summary.thumbnail ?? thumbnailReferences[summary.id])) }
            HUDMotion.perform {
                tile.layer.frame.origin = CGPoint(x: 4 + CGFloat(index % 3) * (tileWidth + 12), y: 4 + CGFloat(index / 3) * pitch)
                area.content.addSublayer(tile.layer)
            }
            page.add(tile); tiles[summary.id] = tile; changed = true
            if stagger { page.stagger(tile.layer) }
        }
        if changed, page === self.page { refreshRouter(); context.onInputStateChange?() }
    }

    private func selectFilter(_ next: Filter) {
        guard next != filter, !isTransitioning else { return }
        let order: [Filter] = [.all, .uncategorized] + controller.categories.map { .category($0.id) }
        let direction: CGFloat = (order.firstIndex(of: next) ?? 0) > (order.firstIndex(of: filter) ?? 0) ? 1 : -1
        keptCategoryOffset = categoryArea?.offset
        filter = next
        navigate(dx: 0, dy: direction)
        if case .category(let id) = next { controller.markCategoryViewed(id) }
    }

    private func addCategory() {
        guard let id = controller.addCategory(), let name = controller.categories.first(where: { $0.id == id })?.name else { return }
        toast(icon: .plus, "Category added", "已添加分类", detail: name)
    }

    private func confirmRemoveCategory() {
        guard case .category(let id) = filter, let category = controller.categories.first(where: { $0.id == id }) else { return }
        confirm(HUDConfirmRequest(id: "removeCategory", title: L10n.text("Remove \(category.name)?", "删除\(category.name)？"),
                                  message: L10n.text("Its documents move to Uncategorized.", "其中的文档将移至未分类。"),
                                  confirmTitle: L10n.text("Remove", "删除"), danger: true, icon: .trash)) { [weak self] in
            guard let self else { return }
            self.filter = .all
            self.controller.deleteCategory(id)
            self.refreshWhenPossible()
            self.toast(icon: .trash, "Category removed", "文档已移至未分类")
        }
    }

    // MARK: Category rename (§3.9.2)

    func beginRename() {
        guard rename == nil, isActive, !isTransitioning, case .category(let id) = filter,
              let category = controller.categories.first(where: { $0.id == id }), let plate = plates[id] else { return }
        if let area = categoryArea { area.reveal(plate.layer.frame) }
        var options = HUDDesignTextField.Options()
        options.maximumLength = ArchiveCategory.maximumNameLength
        options.selectsAllOnBegin = true
        options.inset = CGSize(width: 6, height: 0)
        let field = HUDDesignTextField(id: "categoryName", label: L10n.text("Category name", "分类名称"), text: category.name,
                                       size: CGSize(width: 118, height: 24), options: options, env: env)
        field.onChange = { [weak self, weak field] text in
            guard let self, let field else { return }
            field.setInvalid(self.controller.categoryNameIssue(text, renaming: id) != nil)
        }
        field.onCommit = { [weak self] text in self?.finishRename(id, text) }
        field.onCancel = { [weak self] in self?.finishRename(id, nil) }
        plate.beginRename(field)
        rename = (id, field)
        register(field); page.add(field); refreshRouter()
        field.beginEditing()
    }

    /// Enter or blur commits a valid, changed, unique name; Esc (nil) cancels.
    private func finishRename(_ id: UUID, _ text: String?) {
        guard let current = rename, current.id == id else { return }
        rename = nil
        page.remove(current.field)
        if let text, controller.renameCategory(id, to: text) == .renamed,
           let name = controller.categories.first(where: { $0.id == id })?.name {
            toast(icon: .pen, "Category renamed", "分类已重命名", detail: name)
        }
        if isTransitioning || leaving { needsRefresh = true } else { rerender() }
    }

    // MARK: Opening and closing documents

    private func openDocument(_ id: UUID) {
        guard pendingOpen == nil, !isTransitioning else { return }
        pendingOpen = id
        controller.select(id)
    }

    private func createDocument() {
        guard !isTransitioning, pendingOpen == nil else { return }
        let category: UUID? = { if case .category(let id) = filter { return id }; return nil }()
        reportEdits()
        mediaIndex = 0
        holding { controller.create(categoryID: category) }
        guard controller.selected != nil else { refresh(); return }
        navigate(dx: 1)
    }

    private func closeDocument() {
        guard controller.selected != nil, !isTransitioning else { return }
        finishOpening(announce: false)
        unlocking = nil
        reportEdits()
        holding { controller.select(nil) }
        navigate(dx: -1)
    }

    private func reportEdits() {
        if edited { controller.onEvent?("edited"); edited = false }
    }

    // MARK: Editor (§3.9.3)

    private func buildEditor(_ page: HUDDesignPage) {
        guard let entry = controller.selected else { return }
        let (title, body) = documentParts(for: entry)
        let category = HUDDropdown(id: "category", label: L10n.text("Category", "分类"), value: categoryName(entry.categoryID), width: 178, env: env)
        category.onActivate = { [weak self, weak category] in
            guard let self, let category else { return }
            if self.isMenuOpen { self.closeMenu() } else { self.openCategoryMenu(category) }
        }
        header.configure(crumbs: ["ARCHIVE", categoryName(entry.categoryID)], code: "MOD-04", title: HUDModuleTitle("Document", "文档"),
                         counter: HUDFormat.dotted(entry.date), showsBack: true, actions: [category])
        wireBack(page) { [weak self] in self?.closeDocument() }

        HUDMotion.perform { title.layer.frame.origin = Self.titleRect.origin; page.layer.addSublayer(title.layer) }
        register(title); page.add(title); page.stagger(title.layer)

        let media = HUDButton(id: "media", title: L10n.text("Media", "媒体"), icon: .image, kind: .secondary, size: .small, env: env)
        media.place(at: .zero)
        media.onActivate = { [weak self, weak media] in
            guard let self, let media else { return }
            if self.isMenuOpen { self.closeMenu() } else { self.openMediaMenu(media) }
        }
        let bar = NotesFormatBar(id: "format", state: formatState, canvas: self, env: env, families: ArchiveRichText.families,
                                 fontWidth: 156, trailing: [media])
        bar.onFormat = { [weak self] change in self?.applyFormat(change) }
        HUDMotion.perform { bar.layer.frame.origin = Self.barRect.origin; page.layer.addSublayer(bar.layer) }
        page.add(bar.controls); page.stagger(bar.layer)
        formatBar = bar; body.formatBar = bar

        body.setSize(bodyRect.size)
        HUDMotion.perform { body.layer.frame.origin = bodyRect.origin; page.layer.addSublayer(body.layer) }
        page.add(body); page.stagger(body.layer)
        if !leaving { body.attach() }

        if !entry.media.isEmpty { buildMedia(page, entry) } else { clearMedia() }

        let footer = HUDLayer.plain("archive.footer")
        let classified = controller.selectedClassification != .unclassified
        let protect = HUDButton(id: classified ? "declassify" : "classify",
                                title: classified ? L10n.text("Declassify", "取消机密") : L10n.text("Classify", "设为机密"),
                                icon: classified ? .unlock : .lock, kind: .secondary, size: .small, env: env)
        let delete = HUDButton(id: "delete", title: L10n.text("Delete", "删除"), icon: .trash, kind: .secondary, size: .small, env: env)
        delete.place(at: .zero); protect.place(at: .zero)
        HUDMotion.perform {
            footer.frame = CGRect(x: 0, y: Self.footerY, width: HUDMetric.box.width, height: 26)
            delete.layer.frame.origin = CGPoint(x: HUDMetric.box.width - delete.layer.bounds.width, y: 0)
            protect.layer.frame.origin = CGPoint(x: delete.layer.frame.minX - 8 - protect.layer.bounds.width, y: 0)
            footer.addSublayer(protect.layer); footer.addSublayer(delete.layer)
            page.layer.addSublayer(footer)
        }
        protect.onActivate = { [weak self, weak protect] in
            guard let self, let protect else { return }
            if classified { self.confirmDeclassify() } else { self.openClassify(anchor: protect) }
        }
        delete.onActivate = { [weak self] in self?.confirmDelete() }
        binders.append { [weak self, weak protect, weak delete] in
            guard let self else { return }
            let busy = self.controller.isClassificationBusy
            protect?.isEnabled = !busy; delete?.isEnabled = !busy
        }
        binders.last?()
        page.add([protect, delete]); page.stagger(footer)
        env.applyScale(to: page.layer)
    }

    /// The title field and the body editor of the open document, reused by
    /// in-place re-renders so focus, selection, composition and undo survive.
    private func documentParts(for entry: ArchiveEntry) -> (HUDDesignTextField, ArchiveBodyArea) {
        if documentID == entry.id, let titleField, let bodyArea { return (titleField, bodyArea) }
        discardDocument()
        var options = HUDDesignTextField.Options()
        options.style = HUDTextStyle(size: 18, weight: .bold, color: HUDColor.textOnDark)
        options.maximumLength = 200
        options.inset = CGSize(width: 12, height: 0)
        let title = HUDDesignTextField(id: "title", label: L10n.text("Title", "标题"), text: entry.title,
                                       placeholder: L10n.text("Title", "标题"), size: Self.titleRect.size, options: options, env: env)
        title.onChange = { [weak self] text in self?.titleChanged(text) }
        title.onCommit = { [weak self] _ in self?.controller.flush() }
        title.onTab = { [weak self] backward in if !backward { self?.bodyArea?.focus(selection: nil) } }
        let body = ArchiveBodyArea(id: "body", label: L10n.text("Content", "内容"), placeholder: L10n.text("Start writing", "开始书写"), env: env)
        body.canvas = self
        body.textView.delegate = self
        body.load(entry)
        body.onFocusChange = { [weak self] focused in
            guard let self else { return }
            if focused { self.context.onLockParallax?() }
            self.updateFormatBar()
            self.context.onInputStateChange?()
        }
        documentID = entry.id; titleField = title; bodyArea = body
        return (title, body)
    }

    private func discardDocument() {
        titleField?.endEditing(commit: true)
        bodyArea?.dispose()
        titleField = nil; bodyArea = nil; documentID = nil
    }

    private func titleChanged(_ text: String) {
        guard var entry = controller.selected, entry.id == documentID, !controller.isSelectedLocked else { return }
        let value = String(text.replacingOccurrences(of: "\0", with: "").prefix(200))
        guard value != entry.title else { return }
        // A plain field edits the title; earlier per-run title styling no longer matches it.
        entry.title = value; entry.titleRichText = nil
        controller.update(entry); edited = true
    }

    private func bodyChanged() {
        guard let body = bodyArea, var entry = controller.selected, entry.id == documentID, !controller.isSelectedLocked else { return }
        let view = body.textView
        guard !view.hasMarkedText(), let storage = view.textStorage else { return }
        if view.string.utf8.count > ArchiveStore.maximumBodyBytes {
            var bytes = Data(view.string.utf8.prefix(ArchiveStore.maximumBodyBytes))
            while String(data: bytes, encoding: .utf8) == nil && !bytes.isEmpty { bytes.removeLast() }
            let kept = ((String(data: bytes, encoding: .utf8) ?? "") as NSString).length
            storage.deleteCharacters(in: NSRange(location: kept, length: storage.length - kept))
        }
        entry.body = view.string
        entry.bodyRichText = ArchiveRichText.capture(storage)
        entry.bodyStyle = ArchiveRichText.style(view.typingAttributes)
        controller.update(entry); edited = true
        body.textChanged()
    }

    // MARK: NSTextViewDelegate (the body)

    func textDidChange(_ notification: Notification) {
        guard let view = notification.object as? NSTextView, view === bodyArea?.textView else { return }
        bodyChanged()
    }

    func textViewDidChangeSelection(_ notification: Notification) {
        guard let view = notification.object as? NSTextView, view === bodyArea?.textView else { return }
        updateFormatBar()
    }

    func textDidEndEditing(_ notification: Notification) {
        guard let view = notification.object as? NSTextView, view === bodyArea?.textView else { return }
        controller.flush()
    }

    func textView(_ textView: NSTextView, doCommandBy selector: Selector) -> Bool {
        guard textView === bodyArea?.textView, !textView.hasMarkedText() else { return false }
        if selector == #selector(NSResponder.cancelOperation(_:)) { bodyArea?.blur(); return true }
        return false
    }

    // MARK: Format bar

    /// What the bar shows: the selection (or typing style) while the body
    /// is focused, otherwise the whole document.
    private var formatState: NotesFormatState {
        guard let body = bodyArea else { return NotesFormatState(family: ArchiveRichText.defaultFamily, size: ArchiveRichText.newDocumentSize) }
        return ArchiveRichText.formatState(ArchiveRichText.displayedStyle(in: body.textView, focused: body.isFocused))
    }

    private func updateFormatBar() { formatBar?.show(formatState) }
    var bodyAreaForTesting: ArchiveBodyArea? { bodyArea }

    /// Formatting applies to the selection while the body is focused (the
    /// typing style with no selection), otherwise to the whole document.
    private func applyFormat(_ change: NotesFormatChange) {
        guard let body = bodyArea, !controller.isSelectedLocked else { return }
        let view = body.textView
        let range = body.isFocused ? view.selectedRange() : NSRange(location: 0, length: view.textStorage?.length ?? 0)
        ArchiveRichText.apply(change, to: view, range: range, keepSelection: body.isFocused)
        bodyChanged()
        updateFormatBar()
    }

    // MARK: Header category menu

    private func openCategoryMenu(_ anchor: HUDDropdown) {
        guard let entry = controller.selected else { return }
        let items = [HUDMenu.Item(id: "uncategorized", title: L10n.text("Uncategorized", "未分类"), selected: entry.categoryID == nil)]
            + controller.categories.map { HUDMenu.Item(id: $0.id.uuidString, title: $0.name, selected: $0.id == entry.categoryID) }
        openMenu(id: "moveTo", items: items, anchor: anchor) { [weak self] item in
            guard let self, let current = self.controller.selected, current.id == entry.id else { return }
            let target = item.id == "uncategorized" ? nil : UUID(uuidString: item.id)
            guard target != current.categoryID else { return }
            self.controller.moveSelected(to: target)
            guard self.controller.selected?.categoryID == target else { return }
            self.toast(icon: .folder, "Moved", "已移动", detail: self.categoryName(target))
        }
    }

    // MARK: Delete and classify (§3.9.3, §3.9.4)

    private func confirmDelete() {
        guard let entry = controller.selected else { return }
        confirm(HUDConfirmRequest(id: "deleteDocument", title: L10n.text("Delete this document?", "删除此文档？"),
                                  message: entry.title.isEmpty ? L10n.text("Untitled", "未命名") : entry.title,
                                  confirmTitle: L10n.text("Delete", "删除"), danger: true, icon: .trash)) { [weak self] in
            guard let self, self.controller.selected?.id == entry.id else { return }
            self.edited = false
            self.holding { self.controller.deleteSelected() }
            self.navigate(dx: -1)
            self.toast(icon: .trash, "Document deleted", "文档已删除")
        }
    }

    private func confirmDeclassify() {
        guard controller.selected != nil else { return }
        confirm(HUDConfirmRequest(id: "declassify", title: L10n.text("Remove classification?", "取消机密？"),
                                  message: L10n.text("The password is deleted and the text stays readable.", "密码将被删除，文本保持可读。"),
                                  confirmTitle: L10n.text("Declassify", "取消机密"), icon: .unlock)) { [weak self] in
            self?.controller.declassifySelected { [weak self] result in
                switch result {
                case .success: self?.toast(icon: .unlock, "Declassified", "已取消机密")
                case .failure(let error): self?.toast(icon: .warn, "Declassify failed", "无法取消机密", detail: error.localizedDescription)
                }
            }
        }
    }

    /// Two password fields (≥ 4 characters, matching), Cancel / Classify.
    private func openClassify(anchor: HUDControl) {
        guard let entry = controller.selected else { return }
        endEditing(commit: true)
        let form = passwordForm(title: HUDModuleTitle("Classify", "设为机密"), width: 300, submitID: "classifySubmit",
                                submitTitle: L10n.text("Classify", "设为机密"), extra: nil) { [weak self] password in
            guard let self, self.controller.selected?.id == entry.id else { return }
            self.controller.classifySelected(password: password) { [weak self] result in
                guard let self else { return }
                switch result {
                case .success:
                    self.sealing = entry.id
                    self.closePopover()
                    self.toast(icon: .lock, "Document classified", "已设为机密")
                case .failure(let error):
                    self.toast(icon: .warn, "Classify failed", "无法设为机密", detail: error.localizedDescription)
                }
            }
        }
        openPopover(form.popover, anchor: anchor, controls: form.controls)
        popoverFields = form.fields
        form.fields.first?.beginEditing()
    }

    /// The shared pop-over form of Classify and Recover: New / Confirm password,
    /// then [extra] Cancel and the primary action. Validation turns the
    /// offending field red (too short first, then a mismatch).
    private func passwordForm(title: HUDModuleTitle, width: CGFloat, submitID: String, submitTitle: String,
                              extra: HUDButton?, submit: @escaping (String) -> Void)
        -> (popover: HUDPopover, controls: [HUDControl], fields: [HUDDesignTextField]) {
        let lines = title.lines()
        let contentHeight: CGFloat = 32 + 8 + 32 + 10 + 26
        let popover = HUDPopover(title: lines.title.uppercased(with: Locale(identifier: "en_US_POSIX")), subtitle: lines.subtitle,
                                 width: width, contentHeight: contentHeight, closable: false, env: env)
        var options = HUDDesignTextField.Options()
        options.secure = true; options.inset = CGSize(width: 12, height: 0)
        let inner = width - 24
        let first = HUDDesignTextField(id: "newPassword", label: L10n.text("New password", "新密码"), text: "",
                                       placeholder: L10n.text("New password", "新密码"), size: CGSize(width: inner, height: 32), options: options, env: env)
        let second = HUDDesignTextField(id: "confirmPassword", label: L10n.text("Confirm password", "确认密码"), text: "",
                                        placeholder: L10n.text("Confirm password", "确认密码"), size: CGSize(width: inner, height: 32), options: options, env: env)
        let cancel = HUDButton(id: "passwordCancel", title: L10n.text("Cancel", "取消"), kind: .secondary, size: .small, env: env)
        let action = HUDButton(id: submitID, title: submitTitle, icon: .lock, kind: .primary, size: .small, env: env)
        for button in [cancel, action] + (extra.map { [$0] } ?? []) { button.place(at: .zero) }
        HUDMotion.perform {
            first.layer.frame.origin = .zero
            second.layer.frame.origin = CGPoint(x: 0, y: 40)
            var x = inner
            for button in [action, cancel] + (extra.map { [$0] } ?? []) {
                x -= button.layer.bounds.width
                button.layer.frame.origin = CGPoint(x: x, y: 82)
                x -= 6
            }
            [first.layer, second.layer, cancel.layer, action.layer].forEach(popover.content.addSublayer)
            if let extra { popover.content.addSublayer(extra.layer) }
        }
        env.applyScale(to: popover.layer)
        let validate: () -> Void = { [weak first, weak second, weak action] in
            guard let first, let second, action?.isEnabled == true else { return }
            switch ArchivePassword.issue(first.text, confirmation: second.text) {
            case .tooShort?, .tooLong?:
                first.setInvalid(true); second.setInvalid(false); first.beginEditing(selection: NSRange(location: 0, length: (first.text as NSString).length))
            case .mismatch?:
                first.setInvalid(false); second.setInvalid(true); second.beginEditing(selection: NSRange(location: 0, length: (second.text as NSString).length))
            case nil:
                first.setInvalid(false); second.setInvalid(false)
                submit(first.text)
            }
        }
        first.onTab = { [weak second] _ in second?.beginEditing() }
        second.onTab = { [weak first] _ in first?.beginEditing() }
        first.onReturn = validate; second.onReturn = validate
        for field in [first, second] { field.onCancel = { [weak self] in self?.closePopover() } }
        cancel.onActivate = { [weak self] in self?.closePopover() }
        action.onActivate = validate
        binders.append { [weak self, weak action] in action?.isEnabled = !(self?.controller.isClassificationBusy ?? true) }
        [first, second].forEach(register)
        let controls: [HUDControl] = [first, second, cancel, action] + (extra.map { [$0] } ?? [])
        return (popover, controls, [first, second])
    }

    override func closePopover() {
        // openPopover closes the previous one first: keep the new form's fields.
        guard isPopoverOpen else { super.closePopover(); return }
        let fields = popoverFields
        popoverFields = []
        super.closePopover()
        for field in fields { field.onCancel = nil; field.onCommit = nil; field.endEditing(commit: false) }
        refreshSoon()
    }

    override func closeMenu() {
        super.closeMenu()
        refreshSoon()
    }

    // MARK: Locked view (§3.9.4)

    private func buildLocked(_ page: HUDDesignPage, _ id: UUID) {
        guard let entry = controller.selected else { return }
        let chip = HUDChip(L10n.text("Classified", "机密"), variant: .custom(fill: HUDColor.red, text: HUDColor.white(1)), icon: .lock, env: env)
        let badge = HUDControl(id: "classifiedChip", env: env, layer: chip.layer)
        badge.isEnabled = false
        header.configure(crumbs: ["ARCHIVE", categoryName(entry.categoryID), "CLASSIFIED"], code: "MOD-04",
                         title: HUDModuleTitle("Document", "机密文档"), counter: "LOCKED", showsBack: true, actions: [badge])
        wireBack(page) { [weak self] in self?.closeDocument() }
        let panel = ArchiveLockedPanel(title: entry.title.isEmpty ? L10n.text("Untitled", "未命名") : entry.title, body: entry.body,
                                       size: Self.lockedRect.size, env: env)
        HUDMotion.perform { panel.layer.frame.origin = Self.lockedRect.origin; page.layer.addSublayer(panel.layer) }
        page.stagger(panel.layer)
        lockedPanel = panel
        if sealing == id { sealing = nil; panel.playSealing() }
        switch controller.selectedClassification {
        case .passwordMissing, .keychainUnavailable:
            let status = controller.selectedClassification
            let message = status == .passwordMissing ? ArchiveError.passwordMissing.localizedDescription : ArchiveError.keychainUnavailable.localizedDescription
            let recover = HUDButton(id: "recover", title: L10n.text("Recover", "恢复访问"), icon: .unlock, kind: .primary, env: env)
            recover.onActivate = { [weak self, weak recover] in if let recover { self?.openRecovery(anchor: recover) } }
            var buttons = [recover]
            if status == .keychainUnavailable {
                let retry = HUDButton(id: "retry", title: L10n.text("Retry", "重试"), icon: .refresh, kind: .secondary, env: env)
                retry.onActivate = { [weak self] in self?.controller.refreshSelectedClassification() }
                buttons.insert(retry, at: 0)
            }
            panel.showRecovery(message: message, buttons: buttons)
            binders.append { [weak self] in
                let busy = self?.controller.isClassificationBusy ?? true
                buttons.forEach { $0.isEnabled = !busy }
            }
            page.add(buttons)
        default:
            var options = HUDDesignTextField.Options()
            options.secure = true; options.inset = CGSize(width: 12, height: 0)
            let unlock = HUDButton(id: "unlock", title: L10n.text("Unlock", "解锁"), icon: .unlock, kind: .primary, env: env)
            unlock.place(at: .zero)
            let field = HUDDesignTextField(id: "password", label: L10n.text("Password", "密码"), text: "",
                                           placeholder: L10n.text("Password", "密码"),
                                           size: CGSize(width: panel.fieldWidth(besides: unlock.layer.bounds.width), height: 32),
                                           options: options, env: env)
            field.onReturn = { [weak self] in self?.submitUnlock() }
            unlock.onActivate = { [weak self] in self?.submitUnlock() }
            panel.showPassword(field: field, unlock: unlock)
            register(field)
            page.add([field, unlock])
            passwordField = field; unlockButton = unlock
            binders.append { [weak self, weak unlock, weak field] in
                guard let self else { return }
                let idle = !self.controller.isClassificationBusy && self.opening == nil
                unlock?.isEnabled = idle; field?.isEnabled = self.opening == nil
            }
        }
        binders.forEach { $0() }
        env.applyScale(to: panel.layer)
        focusPasswordSoon()
    }

    /// The password field takes focus whenever the locked view is shown.
    private func focusPasswordSoon() {
        DispatchQueue.main.async { [weak self] in
            guard let self, self.isActive, !self.isTransitioning, !self.isPopoverOpen, !self.isMenuOpen,
                  self.opening == nil, self.unlocking == nil,
                  let field = self.passwordField, !field.isEditing, field.layer.isVisible(in: self.root),
                  !self.context.overlay.capturesInput else { return }
            field.beginEditing()
        }
    }

    private func submitUnlock() {
        guard let field = passwordField, let id = controller.selected?.id, opening == nil, unlocking == nil,
              !controller.isClassificationBusy else { return }
        if field.isEditing { field.endEditing(commit: true) }
        let password = field.text
        unlocking = id
        controller.unlockSelected(password: password) { [weak self] result in
            guard let self else { return }
            self.unlocking = nil
            switch result {
            case .unlocked where self.controller.selected?.id == id:
                self.playOpening(id)
            case .wrongPassword:
                guard let field = self.passwordField else { return }
                field.setInvalid(true)
                self.lockedPanel?.shake()
                if field.isEditing { field.endEditing(commit: true) }
                field.beginEditing(selection: NSRange(location: 0, length: (field.text as NSString).length))
            case .failed(let message):
                self.toast(icon: .warn, "Unlock failed", "无法解锁", detail: message)
                self.refreshWhenPossible()
            default:
                self.refreshWhenPossible()
            }
        }
    }

    /// The bars wipe away line by line, the stamp fades and grows, the bar
    /// drops; then the editor opens in place (spec §3.9.4 `.opening`).
    private func playOpening(_ id: UUID) {
        opening = id
        passwordField?.setText("")
        binders.forEach { $0() }
        if let entry = controller.selected, entry.id == id {
            lockedPanel?.reveal(title: entry.title.isEmpty ? L10n.text("Untitled", "未命名") : entry.title, body: entry.body)
        }
        let duration = lockedPanel?.playOpening() ?? 0
        let work = DispatchWorkItem { [weak self] in self?.finishOpening(announce: true) }
        openingWork = work
        DispatchQueue.main.asyncAfter(deadline: .now() + (env.reducesMotion ? 0 : duration), execute: work)
    }

    private func finishOpening(announce: Bool) {
        guard opening != nil else { return }
        openingWork?.cancel(); openingWork = nil
        opening = nil
        guard announce else { needsRefresh = true; return }
        refreshWhenPossible()
        toast(icon: .unlock, "Document unlocked", "已解除封存")
    }

    /// For a missing or unreadable password: macOS owner authentication, then a
    /// new password (the document stays classified and opens) or Declassify.
    private func openRecovery(anchor: HUDControl) {
        guard let id = controller.selected?.id else { return }
        let declassify = HUDButton(id: "recoverDeclassify", title: L10n.text("Declassify", "取消机密"), icon: .unlock,
                                   kind: .secondary, size: .small, env: env)
        declassify.onActivate = { [weak self] in self?.recover(id, password: nil) }
        let form = passwordForm(title: HUDModuleTitle("Recover", "恢复访问"), width: 330, submitID: "recoverSubmit",
                                submitTitle: L10n.text("Save", "保存"), extra: declassify) { [weak self] password in
            self?.recover(id, password: password)
        }
        binders.append { [weak self, weak declassify] in declassify?.isEnabled = !(self?.controller.isClassificationBusy ?? true) }
        openPopover(form.popover, anchor: anchor, controls: form.controls)
        popoverFields = form.fields
        form.fields.first?.beginEditing()
    }

    private func recover(_ id: UUID, password: String?) {
        guard controller.selected?.id == id, !recovering else { return }
        endEditing(commit: true)
        recovering = true
        controller.recoverSelectedClassification(newPassword: password) { [weak self] result in
            guard let self else { return }
            self.recovering = false
            switch result {
            case .success:
                self.closePopover()
                if password == nil { self.toast(icon: .unlock, "Declassified", "已取消机密") }
                else { self.toast(icon: .unlock, "Access restored", "已恢复访问") }
            case .failure(let error as CocoaError) where error.code == .userCancelled:
                break
            case .failure(let error):
                self.toast(icon: .warn, "Recovery failed", "无法恢复访问", detail: error.localizedDescription)
            }
        }
    }

    // MARK: Media (the Media button, the document's media strip)

    private func openMediaMenu(_ anchor: HUDControl) {
        guard let entry = controller.selected, entry.media.count < 16 else {
            toast(icon: .warn, "Media not added", "未添加媒体", detail: L10n.text("A document holds up to 16 items.", "每个文档最多 16 项。"))
            return
        }
        let choices = shelfChoices?() ?? []
        var items = [HUDMenu.Item(id: "finder", title: L10n.text("Choose in Finder", "从 Finder 选择"))]
        if !choices.isEmpty { items.append(HUDMenu.Item(id: "shelf", title: L10n.text("Choose from Shelf", "从暂存架选择"))) }
        openMenu(id: "media", items: items, anchor: anchor) { [weak self, weak anchor] item in
            guard let self else { return }
            if item.id == "finder" { self.chooseFiles(); return }
            guard let anchor else { return }
            let shelf = choices.map { HUDMenu.Item(id: $0.id.uuidString, title: $0.title, enabled: $0.isSupported && $0.isAvailable) }
            self.openMenu(id: "shelf", items: shelf, anchor: anchor, minimumWidth: 220) { [weak self] item in
                guard let self, let id = UUID(uuidString: item.id) else { return }
                do {
                    guard let access = try self.shelfAccess?(id) else { return }
                    self.importFiles([access.url], release: { access.close() })
                } catch { self.toast(icon: .warn, "Media not added", "未添加媒体", detail: error.localizedDescription) }
            }
        }
    }

    private func chooseFiles() {
        guard let window = context.host?.window, panel == nil else { return }
        let chooser = NSOpenPanel(); chooser.canChooseDirectories = false; chooser.allowsMultipleSelection = true
        if #available(macOS 11.0, *) { chooser.allowedContentTypes = NotesMediaFactory.supportedFileExtensions.compactMap { UTType(filenameExtension: $0) } }
        else { chooser.allowedFileTypes = NotesMediaFactory.supportedFileExtensions }
        panel = chooser
        chooser.beginSheetModal(for: window) { [weak self, weak chooser] result in
            guard let self, let chooser, self.panel === chooser else { return }
            self.panel = nil
            if result == .OK, self.isActive { self.importFiles(chooser.urls) }
        }
    }

    /// Files from the panel, the shelf or a drop on the open document (≤ 16 per document).
    func importFiles(_ urls: [URL], release: (() -> Void)? = nil) {
        guard let selected = controller.selected, !controller.isSelectedLocked, case .editor = renderedStructure?.page else { release?(); return }
        let candidates = Array(urls.prefix(max(0, 16 - selected.media.count)))
        let scopes = candidates.map { $0.startAccessingSecurityScopedResource() }
        let ticket = importTicket
        importQueue.async { [weak self] in
            defer { for (url, scope) in zip(candidates, scopes) where scope { url.stopAccessingSecurityScopedResource() }; release?() }
            var references: [NotesMediaReference] = [], failure: String?
            for url in candidates {
                guard !ticket.isCancelled else { break }
                do { references.append(try NotesMediaFactory.makeReference(from: url)) } catch { failure = error.localizedDescription }
            }
            DispatchQueue.main.async { [weak self] in
                guard let self, !ticket.isCancelled, var entry = self.controller.selected, entry.id == selected.id,
                      !self.controller.isSelectedLocked else { return }
                let added = Array(references.prefix(max(0, 16 - entry.media.count)))
                if let first = added.first {
                    self.mediaIndex = entry.media.count
                    entry.media.append(contentsOf: added)
                    self.controller.update(entry); self.controller.onEvent?("mediaAdded"); self.edited = true
                    self.toast(icon: .image, "Media added", "已添加媒体", detail: added.count == 1 ? first.displayName : "\(first.displayName) +\(added.count - 1)")
                    self.refreshWhenPossible()
                } else if let failure {
                    self.toast(icon: .warn, "Media not added", "未添加媒体", detail: failure)
                }
            }
        }
    }

    private func buildMedia(_ page: HUDDesignPage, _ entry: ArchiveEntry) {
        mediaIndex = min(max(0, mediaIndex), entry.media.count - 1)
        if !leaving { syncMedia(entry) }
        let reference = entry.media[mediaIndex]
        let strip = ArchiveMediaStrip(reference: reference, index: mediaIndex, count: entry.media.count, presentation: presentation,
                                      size: Self.mediaRect.size, env: env)
        HUDMotion.perform { strip.layer.frame.origin = Self.mediaRect.origin; page.layer.addSublayer(strip.layer) }
        strip.previous?.onActivate = { [weak self] in self?.stepMedia(-1) }
        strip.next?.onActivate = { [weak self] in self?.stepMedia(1) }
        strip.play?.onActivate = { [weak self] in self?.presentation?.togglePlayback() }
        strip.remove.onActivate = { [weak self] in self?.removeMedia() }
        strip.seek?.onSeek = { [weak self] fraction in
            guard let media = self?.presentation, let duration = media.reference.duration else { return }
            media.seek(to: duration * fraction)
        }
        progressFill = strip.seek?.fill
        updateProgress()
        page.add(strip.controls); page.stagger(strip.layer)
    }

    private func stepMedia(_ step: Int) {
        guard let entry = controller.selected, !entry.media.isEmpty else { return }
        mediaIndex = (mediaIndex + step + entry.media.count) % entry.media.count
        refreshWhenPossible()
    }

    private func removeMedia() {
        guard var entry = controller.selected, entry.media.indices.contains(mediaIndex), !controller.isSelectedLocked else { return }
        entry.media.remove(at: mediaIndex)
        mediaIndex = min(mediaIndex, max(0, entry.media.count - 1))
        controller.update(entry); controller.onEvent?("mediaRemoved"); edited = true
        refreshWhenPossible()
    }

    private func syncMedia(_ entry: ArchiveEntry) {
        guard !entry.media.isEmpty else { clearMedia(); return }
        let index = min(max(0, mediaIndex), entry.media.count - 1)
        let reference = entry.media[index], key = "\(entry.id):\(index):\(reference.bookmark.hashValue)"
        guard key != presentationKey else { presentation?.setVisible(true); return }
        clearMedia(); presentationKey = key
        let media = NotesMediaPresentation(reference: reference, maximumDimension: 512)
        presentation = media
        media.onStateChange = { [weak self, weak media] in
            guard let self, let media, self.presentation === media, !self.mediaRefreshQueued else { return }
            // Loading reports synchronously from setVisible: coalesce outside the build.
            self.mediaRefreshQueued = true
            DispatchQueue.main.async { [weak self] in
                guard let self else { return }
                self.mediaRefreshQueued = false
                self.refreshWhenPossible()
            }
        }
        media.onProgress = { [weak self] in self?.updateProgress() }
        media.setVisible(true)
    }

    private func clearMedia() {
        let old = presentation
        presentation = nil; presentationKey = nil
        old?.onStateChange = nil; old?.onProgress = nil; old?.dispose()
    }

    private func updateProgress() {
        guard let fill = progressFill, let media = presentation, let duration = media.reference.duration, let track = fill.superlayer else { return }
        HUDMotion.perform { fill.frame.size.width = track.bounds.width * min(1, max(0, media.currentTime / max(0.01, duration))) }
    }

    // MARK: Focus (Windows B14: the field and caret survive a module switch)

    override var isEditingText: Bool { super.isEditingText || bodyArea?.isFocused == true }

    override func endEditing(commit: Bool) {
        super.endEditing(commit: commit)
        bodyArea?.blur()
    }

    private func currentFocus() -> Focus? {
        guard let id = documentID, case .editor(id) = renderedStructure?.page else { return nil }
        if let title = titleField, title.isEditing, let range = title.selectedRange { return Focus(document: id, field: .title, selection: range) }
        if let body = bodyArea, body.isFocused { return Focus(document: id, field: .body, selection: body.textView.selectedRange()) }
        return nil
    }

    private func restoreFocus() {
        guard let focus = focusMemory else { return }
        focusMemory = nil
        guard focus.document == documentID, case .editor(focus.document) = renderedStructure?.page, !isTransitioning else { return }
        switch focus.field {
        case .title: titleField?.beginEditing(selection: focus.selection)
        case .body: bodyArea?.focus(selection: focus.selection)
        }
    }

    // MARK: Input routing

    override func textView(atHost point: CGPoint) -> NSView? {
        if let view = super.textView(atHost: point) { return view }
        guard isActive, !isTransitioning, let body = bodyArea, body.layer.isVisible(in: root) else { return nil }
        if let target = body.pressTarget(host: point) { return target }
        return capturesPointer ? nil : body.hitTest(host: point)
    }

    override func refreshTextFieldProjection() {
        super.refreshTextFieldProjection()
        bodyArea?.refreshProjection()
    }

    @discardableResult
    override func pointerDown(at point: CGPoint, event: NSEvent? = nil) -> Bool {
        guard isActive, !isTransitioning else { return super.pointerDown(at: point, event: event) }
        // The body keeps focus while the format bar edits its selection.
        if let body = bodyArea, body.isFocused, !isMenuOpen, !isPopoverOpen,
           !(formatBar?.formatsSelection(at: point, in: root) ?? false) { body.blur() }
        // A double-click on the selected category plate renames it.
        if event?.clickCount == 2, !isMenuOpen, !isPopoverOpen, rename == nil, case .category(let id) = filter,
           let plate = plates[id], plate.layer.isVisible(in: root), plate.rect(in: root).contains(point), plate.layer.isUnclipped(point, in: root) {
            beginRename(); return true
        }
        return super.pointerDown(at: point, event: event)
    }

    override func handleScroll(at point: CGPoint, delta: CGPoint, event: NSEvent?) -> Bool {
        if let body = bodyArea, body.layer.isVisible(in: root), body.rect(in: root).contains(point) { body.scroll(by: delta.y); return true }
        if let area = gridArea, area.layer.isVisible(in: root), area.layer.frame.contains(point) {
            if area.scroll(by: delta.y) { layoutTiles(in: page); loadVisibleThumbnails() }
            return true
        }
        return false
    }

    override func handleKey(_ event: NSEvent) -> Bool {
        guard event.modifierFlags.intersection([.command, .control, .option]) == .command,
              event.charactersIgnoringModifiers?.lowercased() == "n" else { return false }
        createDocument()
        return true
    }

    /// Documents scrolled out of the grid have no tile; VoiceOver still lists
    /// them (at the grid's frame) and can open them.
    override var accessibilityItems: [HUDAccessibilityItem] {
        let items = super.accessibilityItems
        guard !items.isEmpty, case .list = renderedStructure?.page, let area = gridArea, shownDocuments.count > tiles.count else { return items }
        let frame = root.convert(area.layer.bounds, from: area.layer)
        return items + shownDocuments.filter { tiles[$0.id] == nil }.map { summary in
            let date = HUDFormat.dotted(summary.date)
            let title = summary.isClassified ? L10n.text("Classified document", "机密文档")
                : (summary.title.isEmpty ? L10n.text("Untitled", "未命名") : summary.title)
            return HUDAccessibilityItem(id: "doc:" + summary.id.uuidString, label: title + ", " + date, role: .button, rect: frame)
        }
    }

    override func performAccessibilityAction(_ id: String) {
        if id.hasPrefix("doc:"), let uuid = UUID(uuidString: String(id.dropFirst(4))), tiles[uuid] == nil,
           shownDocuments.contains(where: { $0.id == uuid }) { openDocument(uuid); return }
        super.performAccessibilityAction(id)
    }

    // MARK: Gallery thumbnails

    private func loadVisibleThumbnails() {
        guard isActive, case .list = renderedStructure?.page else { thumbnails.setWanted([]); return }
        let visible = shownDocuments.filter { tiles[$0.id] != nil && !$0.isClassified && $0.mediaCount > 0 }
            .prefix(ArchiveGalleryThumbnails.wantedLimit)
        let ids = Set(visible.map(\.id))
        thumbnailReferences = thumbnailReferences.filter { ids.contains($0.key) }
        thumbnails.setWanted(visible.compactMap { item in (item.thumbnail ?? thumbnailReferences[item.id]).map { (item.id, $0) } })
        let generation = thumbnailGeneration
        for item in visible where item.thumbnail == nil && thumbnailReferences[item.id] == nil
            && !metadataRequests.contains(item.id) && !thumbnails.contains(item.id) {
            metadataRequests.insert(item.id)
            controller.loadThumbnail(for: item.id) { [weak self] reference in
                guard let self, self.thumbnailGeneration == generation else { return }
                self.metadataRequests.remove(item.id)
                guard let reference, self.tiles[item.id] != nil else { return }
                self.thumbnailReferences[item.id] = reference
                self.thumbnails.request(item.id, reference: reference)
            }
        }
    }

    // MARK: Test state

    var formattingField: String? { bodyArea?.isFocused == true ? "body" : (titleField?.isEditing == true ? "title" : nil) }
    var displayedPage: Page { renderedStructure?.page ?? .list }
}

// MARK: - Rich text

/// Archive body formatting (spec §3.9.3): NotesRichText runs drawn with the
/// redesign's faces. The default face is HarmonyOS Sans, stored as no font
/// name (so older documents' system-font runs show in it too); a face
/// without italics leans through obliqueness, which round-trips as italic.
enum ArchiveRichText {
    /// The default text colour (no stored colour): #EEEEEE, dark ink in the light theme.
    static var ink: NSColor { NSColor(cgColor: HUDColor.tone(0xEEEEEE)) ?? .white }
    static let newDocumentSize: Double = 14
    static var defaultFamily: String { HUDFontResolver.latinFamily ?? "HarmonyOS Sans" }
    static let paragraph: NSParagraphStyle = {
        let style = NSMutableParagraphStyle(); style.lineHeightMultiple = 1.3; return style
    }()
    /// The font menu: the default face, the bundled CJK faces, then every installed family.
    static let families: [String] = {
        var names = [defaultFamily]
        for family in HUDFontRegistry.families where family.role == .cjk && !names.contains(family.name) { names.append(family.name) }
        for family in NSFontManager.shared.availableFontFamilies.sorted() where !family.hasPrefix(".") && !names.contains(family) {
            names.append(family)
        }
        return names
    }()

    static func hex(_ color: NotesRGBA?) -> UInt32 {
        guard let color else { return 0xEEEEEE }
        func channel(_ value: Double) -> UInt32 { UInt32((min(1, max(0, value)) * 255).rounded()) }
        return channel(color.red) << 16 | channel(color.green) << 8 | channel(color.blue)
    }

    /// Either theme's default ink (a Theme change can leave the other in an open editor).
    static func isInk(_ color: NSColor) -> Bool {
        guard let value = color.usingColorSpace(.sRGB), value.alphaComponent > 0.996 else { return false }
        let channels = [value.redComponent, value.greenComponent, value.blueComponent]
        return [UInt32(0xEEEEEE), HUDTheme.lightTone(0xEEEEEE)].contains { (hex: UInt32) -> Bool in
            let ink: [CGFloat] = [CGFloat(hex >> 16 & 255) / 255, CGFloat(hex >> 8 & 255) / 255, CGFloat(hex & 255) / 255]
            return zip(channels, ink).allSatisfy { abs($0 - $1) < 0.004 }
        }
    }

    static func family(of style: NotesTextStyle) -> String {
        guard let name = style.fontName, !name.hasPrefix("."), let family = NSFont(name: name, size: 12)?.familyName else { return defaultFamily }
        return family
    }

    static func font(_ style: NotesTextStyle) -> NSFont {
        let size = CGFloat(min(144, max(6, style.fontSize))), manager = NSFontManager.shared
        if let name = style.fontName, !name.hasPrefix("."), var font = NSFont(name: name, size: size), font.familyName != defaultFamily {
            font = style.bold ? manager.convert(font, toHaveTrait: .boldFontMask) : manager.convert(font, toNotHaveTrait: .boldFontMask)
            font = style.italic ? manager.convert(font, toHaveTrait: .italicFontMask) : manager.convert(font, toNotHaveTrait: .italicFontMask)
            return font
        }
        return HUDFontResolver.font(size: size, weight: style.bold ? .bold : .regular, language: HUDTextLanguage.tag()) as NSFont
    }

    static func attributes(_ style: NotesTextStyle) -> [NSAttributedString.Key: Any] {
        let font = font(style)
        var result: [NSAttributedString.Key: Any] = [.font: font, .foregroundColor: style.color?.color ?? ink, .paragraphStyle: paragraph]
        if style.italic, !NSFontManager.shared.traits(of: font).contains(.italicFontMask) { result[.obliqueness] = 0.2 }
        if style.underline { result[.underlineStyle] = NSUnderlineStyle.single.rawValue }
        if style.strikethrough { result[.strikethroughStyle] = NSUnderlineStyle.single.rawValue }
        return result
    }

    static func style(_ attributes: [NSAttributedString.Key: Any]) -> NotesTextStyle {
        let font = attributes[.font] as? NSFont ?? self.font(NotesTextStyle(fontSize: newDocumentSize))
        let traits = NSFontManager.shared.traits(of: font)
        let weight = (CTFontCopyTraits(font) as? [CFString: Any])?[kCTFontWeightTrait] as? CGFloat ?? 0
        var style = NotesTextStyle()
        style.fontName = font.familyName == defaultFamily || font.fontName.hasPrefix(".") ? nil : font.fontName
        style.fontSize = min(144, max(6, Double(font.pointSize)))
        style.bold = traits.contains(.boldFontMask) || weight >= 0.3
        style.italic = traits.contains(.italicFontMask) || ((attributes[.obliqueness] as? NSNumber)?.doubleValue ?? 0) > 0
        if let color = attributes[.foregroundColor] as? NSColor, !isInk(color) { style.color = NotesRGBA(color) }
        style.underline = ((attributes[.underlineStyle] as? NSNumber)?.intValue ?? 0) != 0
        style.strikethrough = ((attributes[.strikethroughStyle] as? NSNumber)?.intValue ?? 0) != 0
        return style
    }

    /// The stored text with its runs; gaps (and invalid runs) take `base`.
    static func attributed(_ text: String, rich: NotesRichText?, base: NotesTextStyle) -> NSAttributedString {
        let result = NSMutableAttributedString(string: text, attributes: attributes(base))
        guard let rich, rich.isValid(for: text) else { return result }
        for run in rich.runs { result.setAttributes(attributes(run.style), range: run.range) }
        return result
    }

    static func capture(_ text: NSAttributedString) -> NotesRichText {
        var result = NotesRichText()
        text.enumerateAttributes(in: NSRange(location: 0, length: text.length)) { attributes, range, _ in
            let style = style(attributes)
            if let last = result.runs.last, last.style == style, last.location + last.length == range.location {
                result.runs[result.runs.count - 1].length += range.length
            } else { result.runs.append(NotesTextRun(location: range.location, length: range.length, style: style)) }
        }
        return result
    }

    /// New, untouched documents type at the spec's 14 px.
    static func typingStyle(_ entry: ArchiveEntry) -> NotesTextStyle {
        var style = entry.bodyStyle
        if entry.body.isEmpty, entry.bodyRichText == nil, style == NotesTextStyle() { style.fontSize = newDocumentSize }
        return style
    }

    /// What the format bar shows: the typing style at a caret, else the
    /// first character's style with B / I / U / S on only when the whole range has them.
    static func displayedStyle(in view: NSTextView, focused: Bool) -> NotesTextStyle {
        guard let storage = view.textStorage, storage.length > 0 else { return style(view.typingAttributes) }
        let selection = view.selectedRange()
        if focused, selection.length == 0 { return style(view.typingAttributes) }
        let range = focused && NSMaxRange(selection) <= storage.length ? selection : NSRange(location: 0, length: storage.length)
        var result = style(storage.attributes(at: range.location, effectiveRange: nil))
        var bold = true, italic = true, underline = true, strikethrough = true
        storage.enumerateAttributes(in: range) { attributes, _, stop in
            let run = style(attributes)
            bold = bold && run.bold; italic = italic && run.italic; underline = underline && run.underline
            strikethrough = strikethrough && run.strikethrough
            if !bold && !italic && !underline && !strikethrough { stop.pointee = true }
        }
        result.bold = bold; result.italic = italic; result.underline = underline; result.strikethrough = strikethrough
        return result
    }

    /// The shared format bar's view of a style.
    static func formatState(_ style: NotesTextStyle) -> NotesFormatState {
        NotesFormatState(family: family(of: style), size: style.fontSize, bold: style.bold, italic: style.italic,
                         underline: style.underline, strikethrough: style.strikethrough, color: hex(style.color))
    }

    /// Applies `change` to `range` (undoable) and to the typing style. Toggles
    /// switch on unless the whole range already has them. Returns true when
    /// stored text attributes changed (the text view reports it as an edit).
    @discardableResult
    static func apply(_ change: NotesFormatChange, to view: NSTextView, range: NSRange, keepSelection: Bool) -> Bool {
        guard let storage = view.textStorage, range.location != NSNotFound, NSMaxRange(range) <= storage.length else { return false }
        var runs: [(NSRange, NotesTextStyle)] = []
        if range.length > 0 { storage.enumerateAttributes(in: range) { attributes, run, _ in runs.append((run, style(attributes))) } }
        let typing = style(view.typingAttributes)
        let current = runs.isEmpty ? [typing] : runs.map(\.1)
        let enable = change.switchesOn(over: current)
        func changed(_ value: NotesTextStyle) -> NotesTextStyle {
            var style = value
            switch change {
            case .font(let family):
                if family == defaultFamily { style.fontName = nil; break }
                var traits: NSFontTraitMask = []
                if style.bold { traits.insert(.boldFontMask) }
                if style.italic { traits.insert(.italicFontMask) }
                let manager = NSFontManager.shared, size = CGFloat(style.fontSize)
                style.fontName = (manager.font(withFamily: family, traits: traits, weight: 5, size: size)
                    ?? manager.font(withFamily: family, traits: [], weight: 5, size: size))?.fontName ?? style.fontName
            case .size(let size): style.fontSize = min(144, max(6, size))
            case .color(let color): style.color = isInk(color) ? nil : NotesRGBA(color)
            case .bold: style.bold = enable
            case .italic: style.italic = enable
            case .underline: style.underline = enable
            case .strikethrough: style.strikethrough = enable
            }
            return style
        }
        view.typingAttributes = attributes(changed(typing))
        let selection = view.selectedRange()
        guard view.setAttributesUndoably(runs.map { ($0.0, attributes(changed($0.1))) }, in: range) else { return false }
        view.setSelectedRange(keepSelection ? range : selection)
        return true
    }
}

// MARK: - Category plate

/// A category plate (spec §3.9.1 `.cat`): light chamfered plate with the name
/// and pad2 count; selected it turns #262626 with a yellow bar. Hover nudges
/// it 3 px right and brightens it (×1.06). NEW marks an unopened category.
final class ArchiveCategoryPlate: HUDControl {
    static let height: CGFloat = 48
    let isSelected: Bool
    private let lit = HUDLayer.plain("category.lit")
    private let name: HUDTextLayer

    init(id: String, title: String, count: Int, isNew: Bool, selected: Bool, width: CGFloat, env: HUDDesignEnvironment) {
        isSelected = selected
        name = HUDTextLayer(title, style: HUDTextStyle(size: 14, weight: .medium, color: selected ? HUDColor.rgb(0xF2F2F2) : HUDColor.nearBlack,
                                                      lineHeight: 1.25))
        super.init(id: id, label: title, env: env)
        let bounds = CGRect(x: 0, y: 0, width: width, height: Self.height)
        HUDMotion.perform {
            layer.bounds = bounds
            let plate = HUDLayer.plain("category.plate")
            plate.frame = bounds
            let colors = selected ? [HUDColor.charcoal, HUDColor.charcoal] : [HUDColor.rgb(0xDCDDD8), HUDColor.rgb(0xCBCCC7)]
            plate.showArtwork(HUDArtwork.plate(colors, size: bounds.size, cut: 10, scale: env.renderScale), scale: env.renderScale)
            lit.frame = bounds; lit.opacity = 0
            lit.showArtwork(HUDArtwork.plate(selected ? [HUDColor.rgb(0x282828), HUDColor.rgb(0x282828)] : [HUDColor.rgb(0xE9EAE5), HUDColor.rgb(0xD7D8D3)],
                                             size: bounds.size, cut: 10, scale: env.renderScale), scale: env.renderScale)
            name.lineFrame = CGRect(x: 16, y: 7, width: width - 16 - 12, height: 17.5)
            let tally = HUDTextLayer(HUDFormat.pad2(count), style: HUDTextStyle(size: 9.5, weight: .bold,
                color: HUDColor.rgb(selected ? 0x9A9A9A : 0x666666), tracking: 0.06, tabular: true))
            tally.lineFrame = CGRect(x: 16, y: 26, width: width - 28, height: 9.5)
            [plate, lit, name, tally].forEach(layer.addSublayer)
            if selected {
                let bar = HUDLayer.plain("category.bar")
                bar.frame = CGRect(x: 6, y: 10, width: 3, height: Self.height - 20); bar.backgroundColor = HUDColor.yellow
                layer.addSublayer(bar)
            }
            if isNew {
                let badge = HUDBadge.new(env: env)
                badge.position = CGPoint(x: width - 14 - badge.bounds.width / 2, y: 5 + badge.bounds.height / 2)
                layer.addSublayer(badge)
            }
        }
        setTarget(bounds, path: HUDShape.chamfer(bounds, cut: 10), container: true)
        env.applyScale(to: layer)
    }

    /// The rename field replaces the name (118 × 24, two px higher).
    func beginRename(_ field: HUDDesignTextField) {
        HUDMotion.perform {
            name.isHidden = true
            field.layer.frame = CGRect(x: 16, y: 5, width: 118, height: 24)
            layer.addSublayer(field.layer)
        }
        env.applyScale(to: field.layer)
    }

    override func hoverChanged() {
        let on = isHovered && isEnabled
        HUDMotion.animate(layer, "transform.translation.x", to: on ? 3 : 0, duration: HUDDuration.hover, env: env)
        HUDMotion.animate(lit, "opacity", to: Float(on ? 1 : 0), duration: HUDDuration.hover, timing: HUDEasing.ease, env: env)
    }
    override var accessibilityRole: HUDAccessibilityItem.Role? { .tab(selected: isSelected) }
}

// MARK: - Body editor

/// The document body's native text view: reports focus, keeps it while a
/// press formats its selection, and leaves the summon shortcut to the HUD.
final class ArchiveBodyTextView: HUDProjectedTextView {
    var onFocusChange: ((Bool) -> Void)?
    /// True refuses to give up focus: the host takes it after a press that
    /// went to an open menu or pop-over, also when it was forwarded.
    var keepsFocus: (() -> Bool)?
    /// Keys go here first while it returns true (an open menu or pop-over).
    var routesKey: ((NSEvent) -> Bool)?
    var onFormatShortcut: ((NotesFormatChange) -> Void)?
    /// `blur()` always lets go.
    fileprivate var releasing = false
    override func becomeFirstResponder() -> Bool {
        let accepted = super.becomeFirstResponder()
        if accepted { DispatchQueue.main.async { [weak self] in self?.onFocusChange?(true) } }
        return accepted
    }
    override func resignFirstResponder() -> Bool {
        if !releasing, keepsFocus?() == true { return false }
        let accepted = super.resignFirstResponder()
        if accepted { DispatchQueue.main.async { [weak self] in self?.onFocusChange?(false) } }
        return accepted
    }
    override func keyDown(with event: NSEvent) {
        if !hasMarkedText(), routesKey?(event) == true { return }
        super.keyDown(with: event)
    }
    override func performKeyEquivalent(with event: NSEvent) -> Bool {
        if SummonShortcut.active.matches(event: event) { return false }
        if window?.firstResponder === self, !hasMarkedText(), let onFormatShortcut, let change = NotesFormatChange(shortcut: event) {
            onFormatShortcut(change); return true
        }
        return super.performKeyEquivalent(with: event)
    }
}

/// The document body (spec §3.9.3 textarea: #1C1C1C, a #3A3A3A ring, padding
/// 12 × 14, `Start writing`). Unlike HUDDesignTextField the projected TextKit
/// editor stays attached while the page is shown, so a click places the caret
/// directly and per-run formatting, IME, undo and VoiceOver remain native.
/// Suspending drops the projected raster but keeps the text and selection.
final class ArchiveBodyArea: HUDControl {
    let textView = ArchiveBodyTextView(frame: .zero)
    weak var canvas: HUDDesignCanvas?
    var onFocusChange: ((Bool) -> Void)?
    /// The owner's format bar: presses on it, and in the menu or pop-over it
    /// opens, format the selection without ending editing (the caret and
    /// selection stay); ⌘B / ⌘I / ⌘U report through its `onFormat`.
    weak var formatBar: NotesFormatBar?
    private let forwarder = NotesPressForwarder()
    private var surface: HUDProjectedTextEditor?
    private let placeholder: HUDTextLayer
    var isFocused: Bool { textView.window != nil && textView.window?.firstResponder === textView }
    var isAttached: Bool { surface != nil }

    init(id: String, label: String, placeholder text: String, env: HUDDesignEnvironment) {
        placeholder = HUDTextLayer(text, style: HUDTextStyle(size: 14, weight: .regular, color: HUDColor.muted))
        super.init(id: id, label: label, env: env)
        HUDMotion.perform {
            layer.backgroundColor = HUDColor.tone(0x1C1C1C)
            layer.borderWidth = 1; layer.borderColor = HUDColor.ringField
            layer.masksToBounds = true
            layer.addSublayer(placeholder)
        }
        textView.isEditable = true; textView.isSelectable = true
        textView.isRichText = true; textView.importsGraphics = false; textView.allowsUndo = true
        textView.insertionPointColor = NSColor(cgColor: HUDColor.accentInk) ?? .yellow
        textView.selectedTextAttributes = [.backgroundColor: NSColor(cgColor: HUDColor.yellow.copy(alpha: 0.35) ?? HUDColor.yellow) ?? .selectedTextBackgroundColor]
        textView.defaultParagraphStyle = ArchiveRichText.paragraph
        textView.setAccessibilityLabel(label)
        textView.onFocusChange = { [weak self] focused in self?.onFocusChange?(focused) }
        textView.keepsFocus = { [weak self] in self?.forwarder.isForwarding ?? false }
        textView.routesKey = { [weak self] event in
            guard let canvas = self?.canvas, canvas.capturesPointer else { return false }
            return canvas.keyDown(event)
        }
        textView.onFormatShortcut = { [weak self] change in self?.formatBar?.onFormat?(change) }
    }

    /// The host's hit test while the body is focused: a press on the format
    /// bar, or anywhere while a menu or pop-over is open (the first press
    /// outside only closes it), goes to a view that never takes focus, so
    /// editing, the caret and the selection stay.
    func pressTarget(host point: CGPoint) -> NSView? {
        guard isFocused, let canvas, let host = canvas.context.host else { return nil }
        if !canvas.capturesPointer {
            guard let formatBar, let box = canvas.context.unproject?(point), formatBar.formatsSelection(at: box, in: canvas.root) else { return nil }
        }
        return forwarder.target(in: host)
    }

    private var textRect: CGRect { layer.bounds.insetBy(dx: 14, dy: 12) }

    func load(_ entry: ArchiveEntry) {
        textView.textStorage?.setAttributedString(ArchiveRichText.attributed(entry.body, rich: entry.bodyRichText, base: entry.bodyStyle))
        textView.typingAttributes = ArchiveRichText.attributes(ArchiveRichText.typingStyle(entry))
        textView.setSelectedRange(NSRange(location: 0, length: 0))
        textChanged()
    }

    func setSize(_ size: CGSize) {
        guard layer.bounds.size != size else { return }
        let attached = surface != nil, focused = isFocused
        surface?.dispose(); surface = nil
        HUDMotion.perform {
            layer.bounds = CGRect(origin: .zero, size: size)
            placeholder.lineFrame = CGRect(x: textRect.minX, y: textRect.minY, width: textRect.width, height: 14 * 1.3)
        }
        setTarget(layer.bounds)
        if attached { attach() }
        if focused { canvas?.context.host?.window?.makeFirstResponder(textView) }
    }

    /// Projects the editor onto the plane; nil hits while the module is not interactive.
    func attach() {
        guard surface == nil, let canvas, let host = canvas.context.host else { return }
        let editor = HUDProjectedTextEditor(textView: textView, rect: textRect, host: host, parent: layer)
        editor.project = { [weak self] rect in
            guard let self, let canvas = self.canvas else { return rect }
            return canvas.context.projected(canvas.root.convert(rect, from: self.layer))
        }
        editor.unproject = { [weak self] point in
            guard let self, let canvas = self.canvas, canvas.isActive, !canvas.isTransitioning, self.layer.isVisible(in: canvas.root),
                  let box = canvas.context.unproject?(point) else { return nil }
            return self.layer.convert(box, from: canvas.root)
        }
        editor.setAppearance(background: .clear, border: .clear, radius: 0)
        surface = editor
        editor.resizeDocument()
        editor.captureVisibleArtwork()
    }

    func suspend() { blur(); surface?.dispose(); surface = nil }

    func dispose() {
        suspend()
        forwarder.removeFromSuperview()
        textView.delegate = nil; textView.onFocusChange = nil; onFocusChange = nil
    }

    func textChanged() {
        HUDMotion.perform { placeholder.isHidden = !textView.string.isEmpty }
        surface?.resizeDocument()
    }

    func focus(selection: NSRange?) {
        attach()
        guard let window = canvas?.context.host?.window else { return }
        window.makeFirstResponder(textView)
        let length = (textView.string as NSString).length
        let start = min(selection?.location ?? length, length)
        let range = NSRange(location: start, length: min(selection?.length ?? 0, length - start))
        textView.setSelectedRange(range); textView.scrollRangeToVisible(range)
    }

    func blur() {
        guard isFocused, let host = canvas?.context.host else { return }
        textView.releasing = true
        host.window?.makeFirstResponder(host)
        textView.releasing = false
    }

    func hitTest(host point: CGPoint) -> NSView? {
        guard let surface, let host = canvas?.context.host else { return nil }
        return surface.hitTest(surface.convert(point, from: host))
    }

    func scroll(by delta: CGFloat) { surface?.scroll(delta: -delta) }
    func refreshProjection() { surface?.refreshProjection() }

    /// A press on the padding focuses the text at its end.
    override func activate() { if isEnabled { focus(selection: nil) } }
    /// The native text view is the accessibility element.
    override var accessibilityRole: HUDAccessibilityItem.Role? { nil }
}

// MARK: - Media strip

/// A seek bar: 4 px #252525 track with a yellow fill; press or drag to seek.
final class ArchiveSeekBar: HUDControl {
    let fill = HUDLayer.plain("seek.fill")
    var onSeek: ((Double) -> Void)?

    init(id: String, label: String, width: CGFloat, env: HUDDesignEnvironment) {
        super.init(id: id, label: label, env: env)
        HUDMotion.perform {
            layer.bounds = CGRect(x: 0, y: 0, width: width, height: 14)
            let track = HUDLayer.plain("seek.track")
            track.frame = CGRect(x: 0, y: 5, width: width, height: 4); track.backgroundColor = HUDColor.sliderTrack
            fill.frame = CGRect(x: 0, y: 0, width: 0, height: 4); fill.backgroundColor = HUDColor.yellow
            track.addSublayer(fill); layer.addSublayer(track)
        }
        setTarget(layer.bounds)
    }

    override func pointerDown(at point: CGPoint) -> Bool { guard isEnabled else { return false }; seek(point); return true }
    override func pointerDragged(to point: CGPoint) { seek(point) }
    override func pointerUp(at point: CGPoint) {}
    private func seek(_ point: CGPoint) { onSeek?(Double(min(1, max(0, point.x / max(1, layer.bounds.width))))) }
    override var accessibilityRole: HUDAccessibilityItem.Role? { nil }
}

/// The open document's media below the body: preview, name and position,
/// previous / next, play / pause (GIF, video), remove, and a video's seek bar.
final class ArchiveMediaStrip {
    let layer = HUDLayer.plain("archive.media")
    private(set) var previous: HUDIconButton?, next: HUDIconButton?, play: HUDIconButton?
    let remove: HUDIconButton
    private(set) var seek: ArchiveSeekBar?
    var controls: [HUDControl] { [previous, next, play].compactMap { $0 } + [remove] + (seek.map { [$0] } ?? []) }

    init(reference: NotesMediaReference, index: Int, count: Int, presentation: NotesMediaPresentation?, size: CGSize,
         env: HUDDesignEnvironment) {
        remove = HUDIconButton(id: "removeMedia", label: L10n.text("Remove media", "移除媒体"), icon: .x, env: env)
        if count > 1 {
            previous = HUDIconButton(id: "mediaPrevious", label: L10n.text("Previous", "上一个"), icon: .left, env: env)
            next = HUDIconButton(id: "mediaNext", label: L10n.text("Next", "下一个"), icon: .right, env: env)
        }
        let playing = presentation?.isPlaying ?? false
        if reference.kind != .image {
            play = HUDIconButton(id: "mediaPlay", label: playing ? L10n.text("Pause", "暂停") : L10n.text("Play", "播放"),
                                 icon: playing ? .pause : .play, env: env)
        }
        HUDMotion.perform {
            layer.bounds = CGRect(origin: .zero, size: size)
            layer.backgroundColor = HUDColor.tone(0x1C1C1C); layer.borderWidth = 1; layer.borderColor = HUDColor.ringField
            let preview = HUDLayer.plain("archive.media.preview")
            // A 16:10 preview as tall as the strip allows; the details sit to its right.
            preview.frame = CGRect(x: 8, y: 8, width: ((size.height - 16) * 1.6).rounded(), height: size.height - 16)
            let left = preview.frame.maxX + 12
            preview.backgroundColor = HUDColor.rgb(0x0E0E0E); preview.masksToBounds = true
            if let presentation { presentation.layer.frame = preview.bounds; preview.addSublayer(presentation.layer) }
            layer.addSublayer(preview)
            let name = HUDTextLayer(reference.displayName, style: HUDTextStyle(size: 13, weight: .medium, color: HUDColor.textOnDark))
            name.lineFrame = CGRect(x: left, y: 12, width: size.width - left - 12, height: 13)
            let failed = presentation?.error.map(\.localizedDescription)
            let detail = HUDTextLayer(failed ?? HUDFormat.pad2(index + 1) + " / " + HUDFormat.pad2(count),
                                      style: HUDTextStyle(size: 9.5, weight: .bold, color: failed == nil ? HUDColor.caption : HUDColor.danger,
                                                          tracking: failed == nil ? 0.06 : 0, tabular: true))
            detail.lineFrame = CGRect(x: left, y: 31, width: size.width - left - 12, height: 9.5)
            layer.addSublayer(name); layer.addSublayer(detail)
            var x = left
            for button in [previous, next, play, remove].compactMap({ $0 }) {
                button.layer.frame = CGRect(x: x, y: size.height - 8 - 30, width: 30, height: 30)
                layer.addSublayer(button.layer); x += 36
            }
            if reference.kind == .video {
                let bar = ArchiveSeekBar(id: "mediaSeek", label: L10n.text("Seek", "进度"), width: size.width - x - 6 - 12, env: env)
                bar.layer.frame.origin = CGPoint(x: x + 6, y: size.height - 8 - 22)
                layer.addSublayer(bar.layer); seek = bar
            }
        }
        env.applyScale(to: layer)
    }
}

// MARK: - Classified document

/// The locked document (spec §3.9.4 `.clsdoc`): hatched bars sized to the
/// title and each wrapped line, the rotated CLASSIFIED stamp and the password
/// bar. Sealing wipes the bars in from the left; opening collapses them to the
/// right line by line, uncovering the text. The text is drawn only once the
/// password is verified (`reveal`), never while the document is locked.
final class ArchiveLockedPanel {
    static var titleStyle: HUDTextStyle { HUDTextStyle(size: 18, weight: .bold, color: HUDColor.foreground(1), lineHeight: 1.3) }
    static var lineStyle: HUDTextStyle { HUDTextStyle(size: 13, weight: .regular, color: HUDColor.tone(0xDDDDDD), lineHeight: 1.45) }
    let layer = HUDLayer.plain("archive.classified")
    let passwordBar = HUDLayer.plain("archive.passwordBar")
    private(set) var bars: [CALayer] = []
    /// The text each bar covers: drawn by `reveal` after a verified password.
    private(set) var revealed: [HUDTextLayer] = []
    private let stamp = HUDLayer.plain("archive.stamp")
    private let env: HUDDesignEnvironment

    init(title: String, body: String, size: CGSize, env: HUDDesignEnvironment) {
        self.env = env
        HUDMotion.perform {
            layer.bounds = CGRect(origin: .zero, size: size)
            layer.backgroundColor = HUDColor.tone(0x141414); layer.borderWidth = 1; layer.borderColor = HUDColor.separator
            layer.masksToBounds = true
            let inner = size.width - 36
            var y: CGFloat = 16
            for (index, line) in ([title] + Self.lines(body)).enumerated() where y < size.height {
                let style = index == 0 ? Self.titleStyle : Self.lineStyle
                let height = style.size * style.lineHeight
                let width = min(inner, max(4, HUDText.width(line, style)))
                let bar = HUDRedaction.bar(size: CGSize(width: width + 6, height: height + 2), labelled: false,
                                           fill: HUDColor.rgb(0x0C0C0C), stripe: HUDColor.charcoal, env: env)
                bar.frame.origin = CGPoint(x: 18 - 3, y: y - 1)
                layer.addSublayer(bar); bars.append(bar)
                y += height + (index == 0 ? 16 : 9)
            }
            let text = HUDTextLayer(L10n.text("CLASSIFIED", "机密"), style: HUDTextStyle(size: 15, weight: .black, color: HUDColor.red, tracking: 0.24),
                                    alignment: .center)
            let width = ceil(text.preferredWidth) + 20 + 6
            stamp.frame = CGRect(x: size.width - 26 - width, y: 22, width: width, height: 15 + 10 + 6)
            stamp.borderWidth = 3; stamp.borderColor = HUDColor.red; stamp.opacity = 0.9
            text.lineFrame = CGRect(x: 0, y: 8, width: width, height: 15)
            stamp.addSublayer(text)
            stamp.transform = Self.stampTransform(scale: 1)
            layer.addSublayer(stamp)
            passwordBar.frame = CGRect(x: 18, y: size.height - 16 - 52, width: size.width - 36, height: 52)
            passwordBar.backgroundColor = HUDColor.tone(0x1A1A1A, 0.96)
            passwordBar.borderWidth = 1; passwordBar.borderColor = HUDColor.ringField
            let edge = HUDLayer.plain(); edge.frame = CGRect(x: 0, y: 0, width: 3, height: 52); edge.backgroundColor = HUDColor.red
            let lock = HUDIconLayer(.lock, size: 18, color: HUDColor.red); lock.frame = CGRect(x: 14, y: 17, width: 18, height: 18)
            passwordBar.addSublayer(edge); passwordBar.addSublayer(lock)
            layer.addSublayer(passwordBar)
        }
        env.applyScale(to: layer)
    }

    /// The body wrapped at 52 characters (only what can be visible).
    static func lines(_ body: String) -> [String] {
        Array(HUDRedaction.wrapped(String(body.prefix(1600)), limit: 52).prefix(16))
    }

    private static func stampTransform(scale: CGFloat) -> CATransform3D {
        CATransform3DScale(CATransform3DMakeRotation(-8 * .pi / 180, 0, 0, 1), scale, scale, 1)
    }

    /// Field width beside an Unlock button of `width` (lock icon, gaps, padding).
    func fieldWidth(besides width: CGFloat) -> CGFloat { passwordBar.bounds.width - 44 - 8 - width - 10 }

    func showPassword(field: HUDDesignTextField, unlock: HUDButton) {
        HUDMotion.perform {
            field.layer.frame.origin = CGPoint(x: 44, y: 10)
            unlock.layer.frame.origin = CGPoint(x: passwordBar.bounds.width - 10 - unlock.layer.bounds.width, y: 10)
            passwordBar.addSublayer(field.layer); passwordBar.addSublayer(unlock.layer)
        }
    }

    /// A missing or unreadable password: the reason and the recovery actions.
    func showRecovery(message: String, buttons: [HUDButton]) {
        HUDMotion.perform {
            var right = passwordBar.bounds.width - 10
            for button in buttons.reversed() {
                button.place(at: .zero)
                right -= button.layer.bounds.width
                button.layer.frame.origin = CGPoint(x: right, y: 10)
                passwordBar.addSublayer(button.layer)
                right -= 8
            }
            let text = HUDTextLayer(message, style: HUDTextStyle(size: 11.5, weight: .regular, color: HUDColor.tone(0xDDDDDD), lineHeight: 1.3))
            text.maximumLines = 2
            text.lineFrame = CGRect(x: 44, y: 11, width: right - 44, height: 30)
            passwordBar.addSublayer(text)
        }
        env.applyScale(to: passwordBar)
    }

    /// Draws the title and lines beneath their bars (the demo's `.cl` text),
    /// so the opening wipe uncovers the document. Only after unlocking.
    func reveal(title: String, body: String) {
        guard revealed.isEmpty else { return }
        HUDMotion.perform {
            for (index, (line, bar)) in zip([title] + Self.lines(body), bars).enumerated() {
                let style = index == 0 ? Self.titleStyle : Self.lineStyle
                let text = HUDTextLayer(line, style: style)
                text.lineFrame = CGRect(x: bar.frame.minX + 3, y: bar.frame.minY + 1, width: layer.bounds.width - 36, height: style.size * style.lineHeight)
                layer.insertSublayer(text, below: bar)
                revealed.append(text)
            }
        }
        env.applyScale(to: layer)
    }

    func playSealing() {
        guard !env.reducesMotion else { return }
        for (index, bar) in bars.enumerated() {
            anchor(bar, x: 0)
            let grow = CABasicAnimation(keyPath: "transform.scale.x")
            grow.fromValue = 0; grow.toValue = 1; grow.duration = 0.4; grow.timingFunction = HUDEasing.standard
            grow.beginTime = bar.convertTime(CACurrentMediaTime(), from: nil) + Double(index) * 0.05; grow.fillMode = .backwards
            bar.add(grow, forKey: "hud.seal")
        }
    }

    /// Returns when the editor may open: n × 70 ms + 520 ms.
    func playOpening() -> TimeInterval {
        for (index, bar) in bars.enumerated() {
            anchor(bar, x: 1)
            HUDMotion.animate(bar, "transform.scale.x", to: 0.001, duration: 0.45, delay: Double(index) * 0.07, env: env)
        }
        HUDMotion.animate(stamp, "opacity", to: Float(0), duration: 0.3, timing: HUDEasing.ease, env: env)
        HUDMotion.animate(stamp, "transform", to: NSValue(caTransform3D: Self.stampTransform(scale: 1.3)), duration: 0.4, env: env)
        HUDMotion.animate(passwordBar, "opacity", to: Float(0), duration: 0.3, timing: HUDEasing.ease, env: env)
        HUDMotion.animate(passwordBar, "transform.translation.y", to: 10, duration: 0.3, env: env)
        return Double(bars.count) * 0.07 + 0.52
    }

    /// Wrong password: ±6 px over .35 s.
    func shake() {
        guard !env.reducesMotion else { return }
        let shake = CAKeyframeAnimation(keyPath: "transform.translation.x")
        shake.values = [0, -6, 6, -6, 6, 0]; shake.keyTimes = [0, 0.2, 0.4, 0.6, 0.8, 1]
        shake.duration = 0.35; shake.isAdditive = true
        passwordBar.add(shake, forKey: "hud.shake")
    }

    private func anchor(_ bar: CALayer, x: CGFloat) {
        HUDMotion.perform { let frame = bar.frame; bar.anchorPoint = CGPoint(x: x, y: 0.5); bar.frame = frame }
    }
}

// MARK: - Gallery posters and imports

/// Static posters only: a bounded set of waiting requests, one native decode
/// and a small LRU. Paging replaces pending work; hiding discards every bitmap.
final class ArchiveGalleryThumbnails {
    static let wantedLimit = 12
    static let cacheLimit = 18
    var onChange: ((UUID, CGImage?) -> Void)?
    private struct Cached { let reference: NotesMediaReference; let image: CGImage? }
    private let queue = DispatchQueue(label: "EndfieldHUD.ArchivePosters", qos: .utility)
    private let decode: (NotesMediaReference) -> CGImage?
    private var cache: [UUID: Cached] = [:], order: [UUID] = []
    private var wanted: [UUID: NotesMediaReference] = [:], pending: [UUID: NotesMediaReference] = [:]
    private var working = false, generation = 0
    private var inFlight: (UUID, NotesMediaReference)?
    var count: Int { cache.count }
    var pendingCount: Int { pending.count }
    init(decode: @escaping (NotesMediaReference) -> CGImage? = { try? NotesMediaFactory.thumbnail(for: $0, maximumDimension: 256) }) { self.decode = decode }
    func contains(_ id: UUID) -> Bool { cache[id] != nil }
    func image(for id: UUID, reference: NotesMediaReference?) -> CGImage? {
        guard let cached = cache[id], reference == nil || cached.reference == reference else { return nil }
        return cached.image
    }
    func setWanted(_ values: [(UUID, NotesMediaReference)]) {
        wanted = Dictionary(values.prefix(Self.wantedLimit), uniquingKeysWith: { _, last in last })
        pending = pending.filter { wanted[$0.key] == $0.value }
        for (id, reference) in wanted where cache[id]?.reference != reference && !(inFlight?.0 == id && inFlight?.1 == reference) { pending[id] = reference }
        pump()
    }
    func request(_ id: UUID, reference: NotesMediaReference) {
        guard wanted[id] != nil || wanted.count < Self.wantedLimit else { return }
        wanted[id] = reference
        if let cached = cache[id], cached.reference == reference { onChange?(id, cached.image); return }
        if inFlight?.0 != id || inFlight?.1 != reference { pending[id] = reference }; pump()
    }
    func clear() { generation += 1; inFlight = nil; wanted.removeAll(); pending.removeAll(); cache.removeAll(); order.removeAll() }
    private func pump() {
        guard !working, let next = pending.first else { return }
        pending.removeValue(forKey: next.key); working = true; inFlight = (next.key, next.value)
        let captured = generation, decode = self.decode
        queue.async { [weak self] in
            let image = decode(next.value)
            DispatchQueue.main.async { [weak self] in
                guard let self else { return }; self.working = false; self.inFlight = nil
                if self.generation == captured, self.wanted[next.key] == next.value {
                    self.cache[next.key] = Cached(reference: next.value, image: image)
                    self.order.removeAll { $0 == next.key }; self.order.append(next.key)
                    while self.order.count > Self.cacheLimit { self.cache.removeValue(forKey: self.order.removeFirst()) }
                    self.onChange?(next.key, image)
                }
                self.pump()
            }
        }
    }
}

final class ArchiveImportTicket {
    private let lock = NSLock()
    private var cancelled = false
    var isCancelled: Bool { lock.lock(); defer { lock.unlock() }; return cancelled }
    func cancel() { lock.lock(); cancelled = true; lock.unlock() }
}
