import AppKit
import QuartzCore

/// The E-Reader (spec §3.15): Open / Library / Reading / Marks. The library,
/// progress, bookmarks and the PDF / EPUB / TXT rendering stay in
/// ReaderController; this canvas presents them. The page renders on the
/// paper tone at 1/1.4 of the panel, so the 10 pt default reads at 14 px.
/// The Reading tab gives the paper almost the whole body; turning a page
/// moves only the paper's pages (left to right: a sliding page; top to
/// bottom: one continuous, freely scrolled column).
final class ReaderCanvas: HUDDesignCanvas {
    enum Tab: Int, CaseIterable { case open, library, reading, marks }
    let controller: ReaderController
    var onEvent: ((ReaderEvent) -> Void)?
    var shelfChoices: (() -> [NotesShelfMediaChoice])?
    var shelfAccess: ((UUID) throws -> ShelfFileAccess)?
    /// The Reader opens where it was left: on the open book when it was being
    /// read (across module switches, HUD reopenings and relaunches).
    private(set) var tab: Tab {
        didSet { if tab != oldValue { tabSettled = true; controller.setReading(tab == .reading) } }
    }
    /// A tab was chosen (or restored); a library that loads later no longer moves it.
    private var tabSettled = false
    private(set) var missing: Set<UUID> = []
    private var missingGeneration = 0
    /// The library (ids and paths) the missing check last looked at.
    private var checkedBooks: [String] = []
    private var openPanel: NSOpenPanel?
    private var panelGeneration = 0
    override var isPresentingPanel: Bool { openPanel != nil }
    override var isInputLocked: Bool { super.isInputLocked || openPanel != nil || panStart != nil }
    private var pendingImport: (name: String, shelf: Bool)?
    private var pendingBookmark: (added: Bool, page: Int)?
    private var pendingRemoval = false
    private var lastError: String?
    private(set) var imageView = ReaderImageView()
    private var imageBook: UUID?
    /// A newly chosen book is opening; the Reading tab waits for its first page.
    private var awaitingBook: UUID?
    private var panStart: (point: CGPoint, pan: CGPoint)?
    private var detailWork: DispatchWorkItem?
    private var wheel = (remainder: CGFloat(0), consumed: false, last: TimeInterval(0))
    /// Page turns started (harness).
    private(set) var pageTurnSequence = 0
    /// A turn waits for its page, or its page motion is running.
    var isTurningPage: Bool { pendingTurn != nil || turnToken != nil }
    /// Left to right: the page shown before a turn, until the next one arrives.
    private var pendingTurn: (from: ReaderLocation, direction: CGFloat)?
    private var turnToken: Int?, turnSerial = 0
    /// Top to bottom: box px from the current page's top to the viewport's top.
    private var scrollOffset: CGFloat = 0
    /// One layer per shown page, keyed by its location and reused while it stays.
    private var pageLayers: [ReaderLocation: CALayer] = [:]
    private var spareLayers: [CALayer] = []
    /// What the column's geometry depends on; a change starts at the page top.
    private var columnKey: ReaderPreferences?
    /// The page `scrollOffset` belongs to; any other way to a page (slider,
    /// bookmark, a reopened book) starts at that page's top.
    private var columnLocation: ReaderLocation?

    /// What the current page's layout depends on; anything else updates in place.
    private struct Structure: Equatable {
        let tab: Tab, language: AppLanguage, books: [BookKey], selected: UUID?, marks: [UUID]
        let hasPage: Bool, loading: Bool, error: String?, pageCount: Int?, awaiting: Bool, vertical: Bool
    }
    private struct BookKey: Equatable { let id: UUID, title: String, progress: Int, missing: Bool }
    private var renderedStructure: Structure?
    private var binders: [() -> Void] = []
    private weak var pageClip: CALayer?
    /// The page layers' parent inside the clip; the column scroll moves it.
    private weak var pageColumn: CALayer?
    private weak var pageNumberText: HUDTextLayer?
    private weak var ribbon: CALayer?
    private weak var pageSlider: HUDSlider?
    private weak var bookmarkButton: HUDIconButton?
    private weak var previousButton: HUDIconButton?
    private weak var nextButton: HUDIconButton?
    private weak var settingsButton: HUDIconButton?

    /// The paper page (box px) between the tabs and the control row, and the
    /// rendered page inside it (paper px): the page's own margins are the
    /// reader's Page margins setting, so the paper adds only its 1 px ring.
    static let pageFrame = CGRect(x: 0, y: HUDMetric.headerHeight + 34 + 6, width: HUDMetric.box.width, height: 351)
    static let pageViewport = CGRect(x: 1, y: 1, width: HUDMetric.box.width - 2, height: 349)
    static let controlsTop = pageFrame.maxY + 6
    static let textScale: CGFloat = 1.4
    static var renderSize: CGSize { CGSize(width: pageViewport.width / textScale, height: pageViewport.height / textScale) }
    /// The rendered page in box px.
    var viewport: CGRect { Self.pageViewport.offsetBy(dx: Self.pageFrame.minX, dy: Self.pageFrame.minY) }

    static let coverColors: [UInt32] = [0x2A5D7A, 0x7A2A2A, 0x555555, 0x6B5A12, 0x3D5A2A, 0x4A3A6B, 0x2A4A4A, 0x6B3A1E]

    init(context: HUDDesignContext, controller: ReaderController) {
        self.controller = controller
        tab = controller.isReading && controller.book != nil ? .reading : .library
        tabSettled = controller.book != nil
        super.init(module: .reader, context: context)
        controller.setLayout(size: Self.renderSize, dark: false)
        controller.onChange = { [weak self] in self?.controllerChanged() }
        controller.onEvent = { [weak self] in self?.handle($0) }
    }

    // MARK: Lifecycle

    override func didActivate() {
        controller.setActive(true)
        refreshMissing()
    }

    override func didDeactivate() {
        closePanel()
        panStart = nil; detailWork?.cancel(); detailWork = nil
        settleTurn()
        controller.setActive(false)
    }

    override func willLeave() { closePanel() }

    /// The page layout of the Reading tab depends on the book, its page count
    /// and the direction, not on page turns, bookmarks or background loading.
    private var structure: Structure {
        let books = controller.books.map { BookKey(id: $0.id, title: $0.title, progress: Int(($0.progress * 100).rounded()), missing: missing.contains($0.id)) }
        let page = controller.current != nil
        return Structure(tab: tab, language: L10n.resolvedLanguage, books: tab == .library ? books : books.map { BookKey(id: $0.id, title: "", progress: 0, missing: false) },
                         selected: controller.book?.id, marks: tab == .marks ? controller.book?.bookmarks.map(\.id) ?? [] : [],
                         hasPage: page, loading: !page && controller.loading, error: page ? nil : controller.error,
                         pageCount: controller.current?.pageCount, awaiting: awaitingBook != nil,
                         vertical: tab == .reading && controller.preferences.vertical)
    }

    private func controllerChanged() {
        if let error = controller.error, error != lastError, isActive { toast(icon: .warn, "Notice", "提示", detail: error) }
        lastError = controller.error
        if isActive, controller.books.map({ $0.id.uuidString + $0.path }) != checkedBooks { refreshMissing() }
        if controller.book?.id != imageBook { imageBook = controller.book?.id; imageView = ReaderImageView() }
        if let awaiting = awaitingBook, controller.book?.id == awaiting, controller.current != nil || controller.error != nil { awaitingBook = nil }
        // The library loaded after this canvas was made: reopen the book that was being read.
        if !tabSettled, controller.book != nil { if controller.isReading { tab = .reading }; tabSettled = true }
        guard renderedLanguage != nil, !isTransitioning else { return }
        if structure != renderedStructure { rerender() } else { binders.forEach { $0() } }
        header.setCounter(counter)
    }

    private func handle(_ event: ReaderEvent) {
        onEvent?(event)
        switch event {
        case .imported:
            guard let pending = pendingImport else { break }
            pendingImport = nil
            if pending.shelf { toast(icon: .folder, "From shelf", "从暂存架添加", detail: pending.name) }
            else { toast(icon: .open, "Book added", "已加入书库", detail: pending.name) }
            refreshMissing()
            if tab != .reading { select(.reading) }
        case .deleted:
            if pendingRemoval { pendingRemoval = false; toast(icon: .trash, "Removed", "已从书库移除") }
        case .bookmarkChanged:
            guard let pending = pendingBookmark else { break }
            pendingBookmark = nil
            if pending.added { toast(icon: .bookmark, "Bookmarked", "已添加书签", detail: "P. \(pending.page)") }
            else { toast(icon: .bookmark, "Bookmark removed", "已移除书签", detail: "P. \(pending.page)") }
        case .preferencesChanged, .progressChanged: break
        }
    }

    private var counter: String {
        if tab == .reading, let number = pageNumber { return "P. \(number.current)" }
        return HUDFormat.pad2(controller.books.count)
    }

    private func select(_ next: Tab) {
        guard next != tab else { return }
        let direction: CGFloat = next.rawValue > tab.rawValue ? 1 : -1
        tab = next
        navigate(dx: direction)
    }

    // MARK: Missing files

    /// Checks every book's file off the main thread; a moved file that its
    /// bookmark still finds is not missing.
    private func refreshMissing() {
        missingGeneration += 1
        let token = missingGeneration, books = controller.books
        checkedBooks = books.map { $0.id.uuidString + $0.path }
        DispatchQueue.global(qos: .utility).async { [weak self] in
            let result = Set(books.filter { !Self.isReachable($0) }.map(\.id))
            DispatchQueue.main.async {
                guard let self, self.missingGeneration == token, result != self.missing else { return }
                self.missing = result
                self.controllerChanged()
            }
        }
    }

    static func isReachable(_ book: ReaderBook) -> Bool {
        if FileManager.default.fileExists(atPath: book.path) { return true }
        var stale = false
        var options: URL.BookmarkResolutionOptions = [.withoutUI, .withoutMounting]
        if book.scoped { options.insert(.withSecurityScope) }
        guard let url = try? URL(resolvingBookmarkData: book.bookmark, options: options, relativeTo: nil, bookmarkDataIsStale: &stale) else { return false }
        let access = url.startAccessingSecurityScopedResource(); defer { if access { url.stopAccessingSecurityScopedResource() } }
        return FileManager.default.fileExists(atPath: url.path)
    }

    // MARK: Pages

    override func buildPage(_ page: HUDDesignPage) {
        binders = []
        renderedStructure = structure
        let tabs = HUDTabs(id: "readerTabs", titles: [(L10n.text("Open", "打开"), .open), (L10n.text("Library", "书库"), .apps),
                                                       (L10n.text("Reading", "阅读"), .text), (L10n.text("Marks", "书签"), .bookmark)],
                           selectedIndex: tab.rawValue, width: HUDMetric.box.width, env: env)
        tabs.onSelect = { [weak self] index, direction in
            guard let self, let next = Tab(rawValue: index) else { return }
            self.tab = next
            self.navigate(dx: direction)
        }
        HUDMotion.perform { tabs.layer.frame.origin = CGPoint(x: 0, y: HUDMetric.headerHeight); page.layer.addSublayer(tabs.layer) }
        page.tabs = tabs
        page.add(tabs.controls); page.stagger(tabs.layer)
        switch tab {
        case .open: buildOpen(page)
        case .library: buildLibrary(page)
        case .reading: buildReading(page)
        case .marks: buildMarks(page)
        }
        // Reading names the open book in the breadcrumb instead of on the paper.
        var crumbs = ["E-READER"]
        if tab == .reading, let title = controller.book?.title, controller.current != nil {
            crumbs.append(title.count > 44 ? String(title.prefix(43)) + "…" : title)
        }
        header.configure(crumbs: crumbs, code: "MOD-10", title: HUDModuleTitle("E-Reader", "阅读器"),
                         counter: counter, showsBack: false)
    }

    private var bodyTop: CGFloat { HUDMetric.headerHeight + 34 + 10 }

    private func buildOpen(_ page: HUDDesignPage) {
        let size = CGSize(width: HUDMetric.box.width, height: 300)
        let panel = HUDEmptyState.make(title: "OPEN A BOOK", subtitle: "PDF / EPUB / TXT", size: size, reserving: 16 + 32, env: env)
        let choose = HUDButton(id: "chooseFile", title: L10n.text("Choose file", "选择文件"), icon: .open, kind: .primary, env: env)
        let shelf = HUDButton(id: "fromShelf", title: L10n.text("From shelf", "从暂存架"), icon: .folder, kind: .secondary, env: env)
        let total = choose.preferredWidth + 8 + shelf.preferredWidth
        let top = HUDEmptyState.textBottom(size: size, subtitle: true, reserving: 16 + 32) + 16
        choose.place(at: CGPoint(x: ((size.width - total) / 2).rounded(), y: top.rounded()))
        shelf.place(at: CGPoint(x: choose.layer.frame.maxX + 8, y: top.rounded()))
        shelf.isEnabled = !availableShelfChoices.isEmpty
        choose.onActivate = { [weak self] in self?.chooseFile() }
        shelf.onActivate = { [weak self, weak shelf] in guard let self, let shelf else { return }; self.openShelfMenu(anchor: shelf) }
        HUDMotion.perform {
            panel.frame.origin = CGPoint(x: 0, y: bodyTop)
            panel.addSublayer(choose.layer); panel.addSublayer(shelf.layer)
            page.layer.addSublayer(panel)
        }
        env.applyScale(to: panel)
        page.add([choose, shelf]); page.stagger(panel)
    }

    private func buildLibrary(_ page: HUDDesignPage) {
        let books = controller.books
        let frame = CGRect(x: 0, y: bodyTop, width: HUDMetric.box.width, height: HUDMetric.box.height - bodyTop)
        guard !books.isEmpty else {
            let empty = HUDEmptyState.make(title: "NO BOOKS", subtitle: "PDF / EPUB / TXT", size: CGSize(width: frame.width, height: 300), env: env)
            HUDMotion.perform { empty.frame.origin = frame.origin; page.layer.addSublayer(empty) }
            page.stagger(empty)
            return
        }
        let scroll = page.scrollArea(id: "library", frame: frame)
        let rows = (books.count + 3) / 4, tileHeight: CGFloat = 120 + 24
        scroll.setContentHeight(4 + CGFloat(rows) * (tileHeight + 12) - 12 + 4)
        let width = scroll.contentWidth - 8, tileWidth = ((width - 3 * 12) / 4).rounded(.down)
        for (index, book) in books.enumerated() {
            let tile = ReaderBookTile(id: "book:\(book.id.uuidString)", number: index + 1, book: book,
                                      cover: HUDColor.rgb(Self.coverColor(book.id)), missing: missing.contains(book.id),
                                      width: tileWidth, env: env)
            tile.setSelected(controller.book?.id == book.id, animated: false)
            tile.onActivate = { [weak self] in self?.openBook(book) }
            tile.more.onActivate = { [weak self, weak tile] in
                guard let self, let tile else { return }
                if self.isMenuOpen { self.closeMenu() } else { self.openBookMenu(book, anchor: tile.more) }
            }
            HUDMotion.perform {
                tile.layer.frame.origin = CGPoint(x: 4 + CGFloat(index % 4) * (tileWidth + 12), y: 4 + CGFloat(index / 4) * (tileHeight + 12))
                scroll.content.addSublayer(tile.layer)
            }
            page.add([tile, tile.more]); page.stagger(tile.layer)
        }
        page.restoreOffset(of: scroll)
    }

    private func buildReading(_ page: HUDDesignPage) {
        let frame = Self.pageFrame
        pageLayers = [:]; spareLayers = []; pendingTurn = nil; turnToken = nil
        guard controller.current != nil, controller.book != nil, awaitingBook == nil else {
            let loading = controller.loading || awaitingBook != nil
            let title = loading ? "LOADING" : (controller.error != nil ? "UNAVAILABLE" : "OPEN A BOOK")
            let empty = HUDEmptyState.make(title: title, subtitle: loading ? nil : "PDF / EPUB / TXT", size: frame.size, env: env)
            HUDMotion.perform { empty.frame.origin = frame.origin; page.layer.addSublayer(empty) }
            page.stagger(empty)
            return
        }
        let paper = HUDLayer.plain("reader.paper"), clip = HUDLayer.plain("reader.viewport"), column = HUDLayer.plain("reader.column")
        let mark = HUDLayer.shape("reader.ribbon")
        HUDMotion.perform {
            paper.frame = frame
            paper.backgroundColor = ReaderDocument.paper
            paper.borderWidth = 1; paper.borderColor = HUDColor.rgb(0xC9C7BF)
            paper.masksToBounds = true
            clip.frame = Self.pageViewport; clip.masksToBounds = true
            column.frame = clip.bounds; clip.addSublayer(column)
            mark.frame = CGRect(x: frame.width - 24 - 16, y: 0, width: 16, height: 26)
            let ribbonPath = CGMutablePath()
            ribbonPath.addLines(between: [.zero, CGPoint(x: 16, y: 0), CGPoint(x: 16, y: 26), CGPoint(x: 8, y: 19.5), CGPoint(x: 0, y: 26)])
            ribbonPath.closeSubpath()
            mark.path = ribbonPath; mark.fillColor = HUDColor.yellow
            [clip, mark].forEach(paper.addSublayer)
            page.layer.addSublayer(paper)
        }
        env.applyScale(to: paper)
        pageClip = clip; pageColumn = column; ribbon = mark
        page.stagger(paper)

        // ◀ slider ▶  37 / 100  bookmark  settings, one 30 px row under the paper.
        let row = HUDLayer.plain("reader.controls")
        let previous = HUDIconButton(id: "previous", label: L10n.text("Previous page", "上一页"), icon: .left, env: env)
        let next = HUDIconButton(id: "next", label: L10n.text("Next page", "下一页"), icon: .right, env: env)
        let bookmark = HUDIconButton(id: "bookmark", label: L10n.text("Bookmark", "书签"), icon: .bookmark, env: env)
        let gear = HUDIconButton(id: "settings", label: L10n.text("Reading settings", "阅读设置"), icon: .gear, env: env)
        let number = HUDTextLayer(style: HUDTextStyle(size: 10, weight: .bold, color: HUDColor.tone(0x9A9A9A), tabular: true), alignment: .right)
        let numberWidth: CGFloat = 64, gap: CGFloat = 8
        let total = pageNumber?.total ?? 100
        let slider = HUDSlider(id: "page", label: L10n.text("Page", "页码"), value: Double(pageNumber?.current ?? 1),
                               range: HUDSlider.Range(minimum: 1, maximum: Double(max(2, total)), step: 1), format: .page,
                               width: HUDMetric.box.width - 4 * 30 - 5 * gap - numberWidth, dark: true, showsReadout: false, showsTicks: false, env: env)
        hostBubble(slider)
        slider.onChange = { [weak self] value, final in
            guard let self, final else { return }
            self.jump(toPage: Int(value.rounded()))
            self.context.onInputStateChange?()
        }
        previous.onActivate = { [weak self] in self?.turn(-1) }
        next.onActivate = { [weak self] in self?.turn(1) }
        bookmark.onActivate = { [weak self] in self?.toggleBookmark() }
        gear.onActivate = { [weak self] in self?.toggleSettings() }
        HUDMotion.perform {
            row.frame = CGRect(x: 0, y: Self.controlsTop, width: HUDMetric.box.width, height: 30)
            var x: CGFloat = 0
            for layer in [previous.layer, slider.layer, next.layer] {
                layer.frame.origin = CGPoint(x: x, y: 0); row.addSublayer(layer)
                x += layer.bounds.width + gap
            }
            number.lineFrame = CGRect(x: x, y: 10, width: numberWidth, height: 10)
            row.addSublayer(number)
            x += numberWidth + gap
            for layer in [bookmark.layer, gear.layer] {
                layer.frame.origin = CGPoint(x: x, y: 0); row.addSublayer(layer)
                x += layer.bounds.width + gap
            }
            page.layer.addSublayer(row)
        }
        env.applyScale(to: row)
        pageNumberText = number
        previousButton = previous; nextButton = next; bookmarkButton = bookmark; pageSlider = slider; settingsButton = gear
        page.add([previous, slider, next, bookmark, gear]); page.stagger(row)
        binders.append { [weak self] in self?.renderPage() }
        renderPage()
    }

    /// The current page into the panel: pages, number, ribbon, slider and button states.
    private func renderPage() {
        guard let current = controller.current else { return }
        let bookmarked = controller.book?.bookmarks.contains { $0.location == current.location } == true
        let number = pageNumber
        HUDMotion.perform {
            pageNumberText?.text = number.map { "\($0.current) / \($0.total)" } ?? ""
            ribbon?.isHidden = !bookmarked
        }
        layoutPages()
        if let slider = pageSlider, !slider.isDragging, let number { slider.setValue(Double(number.current)) }
        let vertical = controller.preferences.vertical
        previousButton?.isEnabled = current.previous != nil || (vertical && scrollOffset > 0)
        nextButton?.isEnabled = current.next != nil
        bookmarkButton?.isEnabled = true
        bookmarkButton?.isOn = bookmarked
    }

    // MARK: Page layers

    /// The viewport (paper px) and the rendered page size (page pt).
    private var viewportSize: CGSize { Self.pageViewport.size }

    /// Top to bottom, the pages drop their top and bottom margins and run on
    /// in a column that keeps one margin above and below on the paper.
    private var columnInset: CGFloat {
        min(viewportSize.height * 0.4, CGFloat(controller.preferences.margin) * Self.textScale).rounded()
    }

    /// A page's height in the column: a text page ends one line spacing
    /// below its last line, so the next page's first line follows on.
    private func columnHeight(_ page: ReaderPage) -> CGFloat {
        let full = viewportSize.height - 2 * columnInset
        guard let text = page.textHeight else { return full }
        return min(full, ((text + CGFloat(controller.preferences.lineSpacing)) * Self.textScale).rounded())
    }

    /// The page's part shown in the column (unit rect, from the top).
    private func columnCrop(_ page: ReaderPage) -> CGRect {
        let top = columnInset / viewportSize.height, height = columnHeight(page) / viewportSize.height
        return CGRect(x: 0, y: top, width: 1, height: height)
    }

    /// The rendered pages around the current one: the page that leads to it
    /// (exactly, when it was read from there) and up to three that follow.
    private func column(_ current: ReaderPage) -> (before: ReaderPage?, after: [ReaderPage]) {
        let pages = controller.pages
        let before = pages.first { $0.next == current.location && $0.location != current.location }
            ?? current.previous.flatMap { location in pages.first { $0.location == location && $0.location != current.location } }
        var after: [ReaderPage] = [], last = current
        while after.count < 3, let next = last.next, let page = pages.first(where: { $0.location == next }) { after.append(page); last = page }
        return (before, after)
    }

    private func pageLayer(_ page: ReaderPage) -> CALayer {
        if let layer = pageLayers[page.location] { return layer }
        let layer = spareLayers.popLast() ?? {
            let made = HUDLayer.plain("reader.page")
            made.contentsGravity = .resize
            HUDMotion.perform { pageColumn?.addSublayer(made) }
            return made
        }()
        HUDMotion.perform { layer.contents = page.image; layer.isHidden = false }
        pageLayers[page.location] = layer
        return layer
    }

    /// Hides every page layer except `keep`; at most six exist.
    private func recycle(keeping keep: Set<ReaderLocation>) {
        for (location, layer) in pageLayers where !keep.contains(location) {
            HUDMotion.perform { layer.removeAllAnimations(); layer.isHidden = true; layer.contents = nil }
            pageLayers[location] = nil
            if spareLayers.count + pageLayers.count < 6 { spareLayers.append(layer) } else { layer.removeFromSuperlayer() }
        }
    }

    /// Lays the shown pages out: one page (left to right, or a zoomed
    /// picture), or the scrolled column (top to bottom).
    private func layoutPages() {
        guard let current = controller.current, pageClip != nil else { return }
        let preferences = controller.preferences
        if columnKey != preferences { columnKey = preferences; scrollOffset = 0 }
        let zoomed = current.isIllustration && imageView.zoom > 1
        if preferences.vertical, !zoomed {
            pendingTurn = nil
            if columnLocation != current.location { columnLocation = current.location; scrollOffset = 0 }
            clampOffset()
            guard let current = controller.current else { return }
            // The whole rendered column stays laid out (at most five pages):
            // a running scroll eases across pages outside the viewport.
            let (before, after) = column(current)
            var placed: [(ReaderPage, CGFloat)] = [(current, -scrollOffset)]
            if let before { placed.append((before, -scrollOffset - columnHeight(before))) }
            var y = -scrollOffset + columnHeight(current)
            for page in after { placed.append((page, y)); y += columnHeight(page) }
            recycle(keeping: Set(placed.map(\.0.location)))
            HUDMotion.perform {
                pageClip?.frame = Self.pageViewport.insetBy(dx: 0, dy: columnInset)
                pageColumn?.frame = pageClip?.bounds ?? .zero
            }
            for (page, top) in placed {
                let layer = pageLayer(page)
                HUDMotion.perform {
                    if layer.contents as! CGImage? !== page.image { layer.contents = page.image }
                    layer.contentsRect = columnCrop(page)
                    layer.frame = CGRect(x: 0, y: top, width: viewportSize.width, height: columnHeight(page))
                }
            }
            return
        }
        if let pending = pendingTurn, pending.from != current.location, !env.reducesMotion {
            pendingTurn = nil
            slide(from: pending.from, to: current, direction: pending.direction)
            return
        }
        if turnToken == nil { recycle(keeping: [current.location]) }
        let layer = pageLayer(current), size = viewportSize
        HUDMotion.perform {
            pageClip?.frame = Self.pageViewport
            pageColumn?.frame = pageClip?.bounds ?? .zero
            layer.contentsRect = CGRect(x: 0, y: 0, width: 1, height: 1)
            if let detail = controller.imageDetail, detail.view == imageView, detail.page.location == current.location {
                layer.contents = detail.page.image
                layer.frame = CGRect(origin: .zero, size: size)
            } else {
                if layer.contents as! CGImage? !== current.image { layer.contents = current.image }
                let zoom = current.isIllustration ? imageView.zoom : 1
                let pan = current.isIllustration ? imageView.pan : .zero
                layer.frame = CGRect(x: size.width * (1 - zoom) / 2 + pan.x * Self.textScale, y: size.height * (1 - zoom) / 2 + pan.y * Self.textScale,
                                     width: size.width * zoom, height: size.height * zoom)
            }
        }
    }

    /// Left to right: the new page slides in over the paper while the old one
    /// leaves; only the page layers move (the paper, header and controls stay).
    private func slide(from old: ReaderLocation, to page: ReaderPage, direction: CGFloat) {
        let width = viewportSize.width
        let outgoing = pageLayers[old]
        recycle(keeping: [old, page.location])
        let incoming = pageLayer(page)
        HUDMotion.perform {
            incoming.contentsRect = CGRect(x: 0, y: 0, width: 1, height: 1)
            incoming.frame = CGRect(origin: .zero, size: viewportSize)
            outgoing?.frame.origin = CGPoint(x: -direction * width, y: 0)
            outgoing?.zPosition = 0; incoming.zPosition = 1
        }
        turnSerial += 1
        let token = turnSerial
        turnToken = token
        CATransaction.begin()
        CATransaction.setCompletionBlock { [weak self] in
            guard let self, self.turnToken == token else { return }
            self.turnToken = nil
            self.recycle(keeping: self.controller.current.map { [$0.location] } ?? [])
        }
        for (layer, from) in [(incoming, direction * width), (outgoing, direction * width)] {
            guard let layer else { continue }
            let move = CABasicAnimation(keyPath: "transform.translation.x")
            move.fromValue = from; move.toValue = 0; move.isAdditive = true
            move.duration = 0.3; move.timingFunction = HUDEasing.standard
            layer.add(move, forKey: "hud.reader.turn")
        }
        CATransaction.commit()
    }

    /// Lands a running or pending page motion at once.
    private func settleTurn() {
        pendingTurn = nil
        guard turnToken != nil else { return }
        turnToken = nil
        HUDMotion.perform { pageColumn?.removeAnimation(forKey: "hud.reader.scroll") }
        if !controller.preferences.vertical { recycle(keeping: controller.current.map { [$0.location] } ?? []) }
        pageLayers.values.forEach { $0.removeAnimation(forKey: "hud.reader.turn") }
    }

    /// Top to bottom: keeps the offset within the rendered column, moving the
    /// current page forward or back as its edge passes the viewport's top.
    private func clampOffset() {
        for _ in 0..<8 {
            guard let current = controller.current else { return }
            let (before, after) = column(current)
            if scrollOffset >= columnHeight(current), let next = after.first {
                scrollOffset -= columnHeight(current); columnLocation = next.location; controller.jump(to: next.location)
            } else if scrollOffset < 0, let before {
                scrollOffset += columnHeight(before); columnLocation = before.location; controller.jump(to: before.location)
            } else {
                let below = after.reduce(columnHeight(current)) { $0 + columnHeight($1) }
                scrollOffset = min(max(0, below - columnHeight(current)), max(0, scrollOffset))
                return
            }
        }
    }

    /// The top of a laid-out column page in the viewport (paper px).
    private func columnTop(of location: ReaderLocation) -> CGFloat? {
        guard let current = controller.current else { return nil }
        if location == current.location { return -scrollOffset }
        let (before, after) = column(current)
        if let before, before.location == location { return -scrollOffset - columnHeight(before) }
        var y = -scrollOffset + columnHeight(current)
        for page in after { if page.location == location { return y }; y += columnHeight(page) }
        return nil
    }

    /// Scrolls the column by `delta` box px; `animated` eases there (keys, buttons).
    private func scrollColumn(by delta: CGFloat, animated: Bool) {
        guard let anchor = controller.current?.location, delta != 0 else { return }
        settleTurn()
        let oldTop = -scrollOffset
        scrollOffset += delta
        clampOffset()
        // A page change already re-rendered through the controller.
        if controller.current?.location == anchor { renderPage() }
        guard animated, !env.reducesMotion, let column = pageColumn, let newTop = columnTop(of: anchor), abs(oldTop - newTop) > 0.5 else { return }
        turnSerial += 1
        let token = turnSerial
        turnToken = token
        CATransaction.begin()
        CATransaction.setCompletionBlock { [weak self] in guard let self, self.turnToken == token else { return }; self.turnToken = nil }
        let move = CABasicAnimation(keyPath: "transform.translation.y")
        move.fromValue = oldTop - newTop; move.toValue = 0; move.isAdditive = true
        move.duration = 0.32; move.timingFunction = HUDEasing.standard
        column.add(move, forKey: "hud.reader.scroll")
        CATransaction.commit()
    }

    private func buildMarks(_ page: HUDDesignPage) {
        let marks = controller.book?.bookmarks ?? []
        let frame = CGRect(x: 0, y: bodyTop, width: HUDMetric.box.width, height: HUDMetric.box.height - bodyTop)
        guard let book = controller.book, !marks.isEmpty else {
            let empty = HUDEmptyState.make(title: "NO BOOKMARKS", env: env)
            HUDMotion.perform { empty.frame.origin = frame.origin; page.layer.addSublayer(empty) }
            page.stagger(empty)
            return
        }
        let scroll = page.scrollArea(id: "marks", frame: frame)
        scroll.setContentHeight(CGFloat(marks.count) * (HUDMetric.rowHeight + HUDMetric.rowGap) - HUDMetric.rowGap)
        let width = scroll.contentWidth
        for (index, mark) in marks.enumerated() {
            let number = Self.page(of: mark, in: book)
            let row = HUDRowPlate(id: "mark:\(mark.id.uuidString)", title: L10n.text("Bookmark \(HUDFormat.pad2(index + 1))", "书签 \(HUDFormat.pad2(index + 1))"),
                                  sublabel: "P. \(number)", width: width, env: env)
            let go = HUDButton(id: "mark:\(mark.id.uuidString):go", title: L10n.text("Go", "前往"), kind: .secondary, size: .small, env: env)
            go.place(at: .zero)
            go.onActivate = { [weak self] in
                guard let self else { return }
                self.controller.jump(to: mark.location)
                self.tab = .reading
                self.navigate(dx: -1)
            }
            let remove = HUDIconButton(id: "mark:\(mark.id.uuidString):remove", label: L10n.text("Remove bookmark", "移除书签") + " P. \(number)", icon: .x, env: env)
            remove.onActivate = { [weak self] in self?.controller.removeBookmark(mark.id) }
            row.setCluster([go.layer, remove.layer])
            HUDMotion.perform {
                row.layer.frame.origin = CGPoint(x: 0, y: CGFloat(index) * (HUDMetric.rowHeight + HUDMetric.rowGap))
                scroll.content.addSublayer(row.layer)
            }
            page.add(row); page.add([go, remove]); page.stagger(row.layer)
        }
        page.restoreOffset(of: scroll)
    }

    // MARK: Library actions

    static func coverColor(_ id: UUID) -> UInt32 {
        let sum = withUnsafeBytes(of: id.uuid) { $0.reduce(0) { $0 &+ Int($1) } }
        return coverColors[sum % coverColors.count]
    }

    /// A book's page in the reader's numbering: PDF pages, otherwise P. 1–100.
    static func page(of mark: ReaderBookmark, in book: ReaderBook) -> Int {
        URL(fileURLWithPath: book.path).pathExtension.lowercased() == "pdf"
            ? mark.location.section + 1 : max(1, Int((mark.progress * 100).rounded()))
    }

    private var pageNumber: (current: Int, total: Int)? {
        guard let page = controller.current else { return nil }
        if let count = page.pageCount { return (page.location.section + 1, count) }
        return (max(1, Int((page.progress * 100).rounded())), 100)
    }

    private func openBook(_ book: ReaderBook) {
        if missing.contains(book.id) { relink(book); return }
        if controller.book?.id != book.id { awaitingBook = book.id; controller.select(book.id) }
        tab = .reading
        navigate(dx: 1)
    }

    private func openBookMenu(_ book: ReaderBook, anchor: HUDControl) {
        let items = [HUDMenu.Item(id: "open", title: L10n.text("Open", "打开")),
                     HUDMenu.Item(id: "relink", title: L10n.text("Relink…", "重新链接…")),
                     HUDMenu.Item(id: "remove", title: L10n.text("Remove from library", "从书库移除"), danger: true, separatorBefore: true)]
        openMenu(id: "bookMenu", items: items, anchor: anchor, minimumWidth: 170) { [weak self] item in
            guard let self else { return }
            switch item.id {
            case "open": self.openBook(book)
            case "relink": self.relink(book)
            default: self.confirmRemove(book)
            }
        }
    }

    private func confirmRemove(_ book: ReaderBook) {
        confirm(HUDConfirmRequest(id: "readerRemove", title: L10n.text("Remove \(book.title) from the library?", "从书库移除 \(book.title)？"),
                                  message: L10n.text("The file itself stays on disk.", "文件本身仍保留在磁盘上。"),
                                  confirmTitle: L10n.text("Remove", "移除"), danger: true, icon: .trash)) { [weak self] in
            self?.pendingRemoval = true
            self?.controller.removeBook(book.id)
        }
    }

    private func relink(_ book: ReaderBook) {
        presentPanel(title: L10n.text("Locate \(book.title)", "查找 \(book.title)")) { [weak self] url in
            guard let self else { return }
            self.controller.relink(book.id, url: url) { [weak self] success in
                guard let self, success else { return }
                self.missing.remove(book.id)
                self.toast(icon: .link2, "Book relinked", "已重新链接", detail: book.title)
                self.refreshMissing()
                self.controllerChanged()
            }
        }
    }

    // MARK: Open actions

    private var availableShelfChoices: [NotesShelfMediaChoice] { (shelfChoices?() ?? []).filter { $0.isSupported && $0.isAvailable } }

    private func chooseFile() {
        presentPanel(title: nil) { [weak self] url in self?.importBook(url, shelf: false) }
    }

    /// A dropped or chosen file opens and joins the library.
    func importFiles(_ urls: [URL]) {
        guard isActive, let url = urls.first else { return }
        closePopups()
        importBook(url, shelf: false)
    }

    private func importBook(_ url: URL, shelf: Bool, access: ShelfFileAccess? = nil) {
        pendingImport = (url.lastPathComponent, shelf)
        controller.open(url: url, retainedAccess: access)
    }

    private func openShelfMenu(anchor: HUDControl) {
        let choices = availableShelfChoices
        guard !choices.isEmpty else { return }
        let items = choices.map { HUDMenu.Item(id: $0.id.uuidString, title: $0.title) }
        openMenu(id: "shelf", items: items, anchor: anchor, minimumWidth: 220) { [weak self] item in
            guard let self, let id = UUID(uuidString: item.id), let choice = choices.first(where: { $0.id == id }) else { return }
            do {
                guard let access = try self.shelfAccess?(id) else { return }
                self.pendingImport = (choice.title, true)
                self.controller.open(url: access.url, retainedAccess: access)
            } catch { self.toast(icon: .warn, "Notice", "提示", detail: error.localizedDescription) }
        }
    }

    private func presentPanel(title: String?, chosen: @escaping (URL) -> Void) {
        guard openPanel == nil, let window = context.host?.window else { return }
        let panel = NSOpenPanel()
        if let title { panel.message = title }
        panel.allowsMultipleSelection = false; panel.canChooseDirectories = false
        panel.allowedFileTypes = ReaderDocument.extensions
        panel.level = NSWindow.Level(rawValue: max(panel.level.rawValue, window.level.rawValue + 1))
        panelGeneration += 1
        let token = panelGeneration
        openPanel = panel
        context.onInputStateChange?()
        panel.beginSheetModal(for: window) { [weak self, weak panel] response in
            guard let self, self.panelGeneration == token else { return }
            self.openPanel = nil
            self.context.onInputStateChange?()
            guard self.isActive, response == .OK, let url = panel?.url else { return }
            chosen(url)
        }
    }

    private func closePanel() {
        panelGeneration += 1
        let panel = openPanel; openPanel = nil; panel?.cancel(nil)
    }

    // MARK: Reading actions

    /// One page forward or back. Left to right the page slides; top to bottom
    /// the column eases to the next page's top (or back to this or the
    /// previous page's top).
    private func turn(_ direction: Int) {
        guard tab == .reading, controller.active, let current = controller.current else { return }
        if controller.preferences.vertical, !(current.isIllustration && imageView.zoom > 1) {
            let (before, after) = column(current)
            if direction > 0 {
                guard current.next != nil else { return }
                if after.isEmpty { controller.next() } else { scrollColumn(by: columnHeight(current) - scrollOffset, animated: true) }
            } else if scrollOffset > 0.5 { scrollColumn(by: -scrollOffset, animated: true) }
            else if let before { scrollColumn(by: -columnHeight(before), animated: true) }
            else if current.previous != nil { controller.previous() }
            else { return }
            pageTurnSequence += 1
            return
        }
        guard direction > 0 ? current.next != nil : current.previous != nil else { return }
        settleTurn()
        if !env.reducesMotion { pendingTurn = (current.location, CGFloat(direction)) }
        if direction > 0 { controller.next() } else { controller.previous() }
        pageTurnSequence += 1
    }

    private func jump(toPage number: Int) {
        guard let current = controller.current else { return }
        settleTurn()
        if let count = current.pageCount { controller.jump(progress: (Double(number) - 0.5) / Double(max(1, count))) }
        else { controller.jump(progress: number <= 1 ? 0 : Double(number) / 100) }
    }

    private func toggleBookmark() {
        guard let current = controller.current, let book = controller.book, let number = pageNumber else { return }
        let added = !book.bookmarks.contains { $0.location == current.location }
        pendingBookmark = (added, number.current)
        controller.toggleBookmark()
    }

    /// Settings pop-over (spec §3.15): direction, font, font size, line
    /// spacing and page margins. Values change in place while it is open.
    private func toggleSettings() {
        if isPopoverOpen { closePopover(); return }
        guard let anchor = settingsButton else { return }
        openSettings(anchor: anchor)
    }

    private func openSettings(anchor: HUDControl) {
        let width: CGFloat = 270, inner = width - 24, rowHeight: CGFloat = 26
        let steppers: [(id: String, title: String, key: WritableKeyPath<ReaderPreferences, Double>, range: ClosedRange<Double>)] = [
            ("fontSize", L10n.text("Font size", "字号"), \.fontSize, 10...32),
            ("lineSpacing", L10n.text("Line spacing", "行间距"), \.lineSpacing, 0...18),
            ("margin", L10n.text("Page margins", "页边距"), \.margin, 6...48)]
        let popover = HUDPopover(title: "", width: width, contentHeight: 30 + 10 + CGFloat(steppers.count + 1) * (rowHeight + 8) - 8,
                                 closable: false, env: env)
        var controls: [HUDControl] = []
        let direction = HUDSegmentedControl(id: "direction", label: L10n.text("Reading direction", "阅读方向"),
                                            options: [L10n.text("Left to right", "从左到右"), L10n.text("Top to bottom", "从上到下")],
                                            selectedIndex: controller.preferences.vertical ? 1 : 0, width: inner, env: env)
        direction.onChange = { [weak self] index in self?.updatePreferences { $0.vertical = index == 1 } }
        // The pop-over retains the group: its segments only hold it weakly.
        controls.append(direction); controls += direction.segments
        var y: CGFloat = 40
        let font = HUDDropdown(id: "font", label: L10n.text("Font", "字体"), value: controller.preferences.fontName, size: .small, width: 150, env: env)
        font.onActivate = { [weak self, weak font] in guard let self, let font else { return }; self.openFontMenu(anchor: font) }
        controls.append(font)
        var rows: [(String, [CALayer])] = [(L10n.text("Font", "字体"), [font.layer])]
        var updates: [() -> Void] = []
        for stepper in steppers {
            let value = HUDTextLayer(style: HUDTextStyle(size: 12, weight: .bold, color: HUDColor.textOnDark, tabular: true), alignment: .center)
            let minus = HUDIconButton(id: stepper.id + ":minus", label: stepper.title + " −", icon: .minus, side: 26, iconSize: 11, env: env)
            let plus = HUDIconButton(id: stepper.id + ":plus", label: stepper.title + " +", icon: .plus, side: 26, iconSize: 11, env: env)
            let group = HUDLayer.plain("reader.stepper")
            HUDMotion.perform {
                group.bounds = CGRect(x: 0, y: 0, width: 26 + 36 + 26, height: rowHeight)
                minus.layer.frame.origin = .zero; plus.layer.frame.origin = CGPoint(x: 26 + 36, y: 0)
                value.lineFrame = CGRect(x: 26, y: 7, width: 36, height: 12)
                [minus.layer, value, plus.layer].forEach(group.addSublayer)
            }
            let update = { [weak self, weak value, weak minus, weak plus] in
                guard let self else { return }
                let current = self.controller.preferences[keyPath: stepper.key]
                HUDMotion.perform { value?.text = String(Int(current.rounded())) }
                minus?.isEnabled = current > stepper.range.lowerBound
                plus?.isEnabled = current < stepper.range.upperBound
            }
            for (button, step) in [(minus, -1.0), (plus, 1.0)] {
                button.onActivate = { [weak self] in
                    self?.updatePreferences { $0[keyPath: stepper.key] = min(stepper.range.upperBound, max(stepper.range.lowerBound, $0[keyPath: stepper.key] + step)) }
                    updates.forEach { $0() }
                }
            }
            updates.append(update)
            controls += [minus, plus]
            rows.append((stepper.title, [group]))
        }
        HUDMotion.perform {
            direction.layer.frame.origin = .zero
            popover.content.addSublayer(direction.layer)
            for (title, layers) in rows {
                let label = HUDTextLayer(title, style: HUDTextStyle(size: 12.5, weight: .regular, color: HUDColor.tone(0xDDDDDD)))
                label.lineFrame = CGRect(x: 0, y: y + (rowHeight - 12.5) / 2, width: 120, height: 12.5)
                popover.content.addSublayer(label)
                var right = inner
                for layer in layers.reversed() {
                    right -= layer.bounds.width
                    layer.frame.origin = CGPoint(x: right, y: y + (rowHeight - layer.bounds.height) / 2)
                    popover.content.addSublayer(layer)
                }
                y += rowHeight + 8
            }
        }
        updates.forEach { $0() }
        env.applyScale(to: popover.layer)
        openPopover(popover, anchor: anchor, controls: controls)
    }

    private func openFontMenu(anchor: HUDControl) {
        let current = controller.preferences.fontName
        let items = NSFontManager.shared.availableFontFamilies.sorted().map { HUDMenu.Item(id: $0, title: $0, selected: $0 == current) }
        openMenu(id: "font", items: items, anchor: anchor, minimumWidth: 200) { [weak self] item in
            guard let self else { return }
            self.updatePreferences { $0.fontName = item.id }
            if let gear = self.settingsButton { self.openSettings(anchor: gear) }
        }
    }

    private func updatePreferences(_ change: (inout ReaderPreferences) -> Void) {
        var next = controller.preferences
        change(&next)
        guard next.isValid, next != controller.preferences else { return }
        controller.setPreferences(next)
    }

    // MARK: Zoom and wheel (illustrated pages)

    private func renderPoint(_ point: CGPoint) -> CGPoint {
        let origin = viewport.origin, size = Self.renderSize
        return CGPoint(x: (point.x - origin.x) / Self.textScale - size.width / 2, y: (point.y - origin.y) / Self.textScale - size.height / 2)
    }

    private func zoom(by amount: CGFloat, at point: CGPoint) {
        guard controller.current?.isIllustration == true, amount.isFinite else { return }
        // A zoomed picture fills the viewport on its own, the column at its top.
        if imageView.zoom <= 1 { scrollOffset = 0; settleTurn() }
        let old = imageView.zoom, nextZoom = min(12, max(1, old * amount)), ratio = nextZoom / old
        let p = renderPoint(point)
        var view = imageView; view.zoom = nextZoom
        view.pan = CGPoint(x: p.x - (p.x - view.pan.x) * ratio, y: p.y - (p.y - view.pan.y) * ratio)
        setImageView(view)
    }

    private func setImageView(_ value: ReaderImageView) {
        let value = value.clamped(to: Self.renderSize)
        guard value != imageView else { return }
        imageView = value
        controller.cancelImageDetail()
        layoutPages()
        detailWork?.cancel(); detailWork = nil
        guard value.zoom > 1 else { return }
        // One debounced, viewport-sized detail raster after the gesture.
        let work = DispatchWorkItem { [weak self] in
            guard let self, self.isActive, self.imageView == value else { return }
            self.detailWork = nil; self.controller.requestImageDetail(value)
        }
        detailWork = work
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.12, execute: work)
    }

    override func handleMagnify(at point: CGPoint, magnification: CGFloat) -> Bool {
        guard tab == .reading, viewport.contains(point), controller.current?.isIllustration == true, magnification.isFinite else { return false }
        zoom(by: max(0.05, 1 + magnification), at: point)
        return true
    }

    override func handleScroll(at point: CGPoint, delta: CGPoint, event: NSEvent?) -> Bool {
        guard tab == .reading, Self.pageFrame.contains(point), let current = controller.current else { return false }
        let zoomKey = event.map { !$0.modifierFlags.intersection([.option, .command]).isEmpty } ?? false
        if zoomKey, current.isIllustration { zoom(by: exp(-delta.y * 0.012), at: point); return true }
        if current.isIllustration, imageView.zoom > 1 {
            var view = imageView
            view.pan.x -= delta.x / Self.textScale; view.pan.y -= delta.y / Self.textScale
            setImageView(view); return true
        }
        // Top to bottom is one continuous column: every delta, momentum included, moves it.
        if controller.preferences.vertical {
            if delta.y != 0, abs(delta.y) >= abs(delta.x) { scrollColumn(by: delta.y, animated: false) }
            return true
        }
        let phase = event?.phase ?? [], momentum = event?.momentumPhase ?? []
        if phase.contains(.began) { wheel.remainder = 0; wheel.consumed = false }
        if !momentum.isEmpty { return true }
        if phase.contains(.ended) || phase.contains(.cancelled) { wheel.remainder = 0; return true }
        let amount = delta.x, cross = delta.y
        guard abs(amount) > abs(cross) * 1.1 else { return true }
        let now = event?.timestamp ?? ProcessInfo.processInfo.systemUptime
        if phase.isEmpty, now - wheel.last >= 0.24 { wheel.consumed = false }
        guard !wheel.consumed else { return true }
        wheel.remainder += amount
        if abs(wheel.remainder) >= (event?.hasPreciseScrollingDeltas == true ? 60 : 16) {
            wheel.consumed = true; wheel.last = now
            turn(wheel.remainder > 0 ? 1 : -1); wheel.remainder = 0
        }
        return true
    }

    override func handlePointerDown(at point: CGPoint, event: NSEvent?) -> Bool {
        guard tab == .reading, viewport.contains(point) else { return false }
        if controller.current?.isIllustration == true, imageView.zoom > 1 { panStart = (point, imageView.pan); context.onLockParallax?() }
        return true
    }

    override func handlePointerDragged(to point: CGPoint) {
        guard let start = panStart else { return }
        var view = imageView
        view.pan = CGPoint(x: start.pan.x + (point.x - start.point.x) / Self.textScale, y: start.pan.y + (point.y - start.point.y) / Self.textScale)
        setImageView(view)
    }

    override func handlePointerUp(at point: CGPoint) { panStart = nil }

    // MARK: Keys

    /// Esc first resets a zoomed page.
    override func handleKey(_ event: NSEvent) -> Bool {
        guard tab == .reading, event.keyCode == 53, imageView.zoom > 1,
              event.modifierFlags.intersection([.command, .control, .option, .shift]).isEmpty else { return false }
        setImageView(ReaderImageView())
        return true
    }

    /// Reading tab: ← / → turn pages (↑ / ↓ too when reading top to bottom; Space goes forward).
    override func handleExtraKey(_ event: NSEvent) -> Bool {
        guard tab == .reading, event.modifierFlags.intersection([.command, .control, .option, .shift]).isEmpty else { return false }
        let vertical = controller.preferences.vertical
        switch event.keyCode {
        case 123: turn(-1)
        case 124, 49: turn(1)
        case 126 where vertical: turn(-1)
        case 125 where vertical: turn(1)
        default: return false
        }
        return true
    }

    // MARK: Accessibility and harness

    /// The page's text summary for VoiceOver; activating it turns forward.
    override var accessibilityItems: [HUDAccessibilityItem] {
        var items = super.accessibilityItems
        if tab == .reading, isActive, !isTransitioning, let current = controller.current {
            let label = current.summary.isEmpty ? (pageNumber.map { "P. \($0.current) / \($0.total)" } ?? "") : current.summary
            items.append(HUDAccessibilityItem(id: "page", label: label, role: .button, rect: viewport, enabled: current.next != nil))
        }
        return items
    }

    override func performAccessibilityAction(_ id: String) {
        if id == "page" { turn(1) } else { super.performAccessibilityAction(id) }
    }
}

/// A Library book tile (spec §2.15 variant, §3.15): the archive tile's well,
/// code bar and label strip with a rotated cover, the format tag, a progress
/// bar, the MISSING overlay and the ⋯ button; the open book has the brackets.
final class ReaderBookTile: HUDControl {
    final class More: HUDControl {
        fileprivate let plate = HUDLayer.plain("book.more")
        override func hoverChanged() {
            HUDMotion.animate(plate, "backgroundColor", to: isHovered && isEnabled ? HUDColor.rgb(0x141414, 0.95) : HUDColor.rgb(0x141414, 0.8),
                              duration: HUDDuration.hover, timing: HUDEasing.ease, env: env)
            HUDMotion.animate(layer, "transform.scale", to: isHovered && isEnabled ? 1.08 : 1, duration: HUDDuration.hover, env: env)
        }
    }

    let more: More
    private let well = HUDLayer.plain("book.well")
    private let wellBrighten = HUDLayer.plain()
    private let strip = HUDLayer.plain("book.label")
    private let labelText: HUDTextLayer
    private let brackets = HUDLayer.shape("book.brackets")
    private(set) var isSelected = false
    static let wellHeight: CGFloat = 120

    init(id: String, number: Int, book: ReaderBook, cover color: CGColor, missing: Bool, width: CGFloat, env: HUDDesignEnvironment) {
        labelText = HUDTextLayer(book.title, style: HUDTextStyle(size: 11.5, weight: .medium, color: HUDColor.nearBlack), alignment: .center)
        more = More(id: id + ":more", label: L10n.text("More", "更多") + ", " + book.title, env: env)
        super.init(id: id, label: book.title, env: env)
        let wellHeight = Self.wellHeight
        let bounds = CGRect(x: 0, y: 0, width: width, height: wellHeight + 24)
        HUDMotion.perform {
            layer.bounds = bounds
            well.frame = CGRect(x: 0, y: 0, width: width, height: wellHeight); well.masksToBounds = true
            well.showArtwork(HUDArtwork.plate([HUDColor.rgb(0x8A8885), HUDColor.rgb(0xBDBDBB), HUDColor.rgb(0xD9D9D8)], locations: [0, 0.55, 1],
                                              size: well.bounds.size, cut: 0, scale: env.renderScale), scale: env.renderScale)
            wellBrighten.frame = well.bounds; wellBrighten.backgroundColor = HUDColor.white(0.12); wellBrighten.opacity = 0
            let codeBar = HUDLayer.plain(); codeBar.frame = CGRect(x: 0, y: 0, width: 34, height: 4); codeBar.backgroundColor = HUDColor.counterBackground
            let code = HUDTextLayer("#" + HUDFormat.pad2(number), style: HUDTextStyle(size: 6, weight: .bold, color: HUDColor.rgb(0x222222)))
            code.lineFrame = CGRect(x: 38, y: -1, width: 30, height: 6)
            let cover = HUDLayer.plain("book.cover")
            cover.bounds = CGRect(x: 0, y: 0, width: 58, height: 82); cover.position = CGPoint(x: width / 2, y: wellHeight / 2)
            cover.backgroundColor = color
            cover.shadowColor = HUDColor.black(1); cover.shadowOpacity = 0.35; cover.shadowRadius = 7; cover.shadowOffset = CGSize(width: 0, height: 8)
            cover.shadowPath = CGPath(rect: cover.bounds, transform: nil)
            let coverTitle = HUDTextLayer(book.title, style: HUDTextStyle(size: 9, weight: .black, color: HUDColor.white(1), lineHeight: 1.05))
            coverTitle.maximumLines = 4
            coverTitle.lineFrame = CGRect(x: 6, y: 82 - 6 - 9 * 1.05 * 4, width: 46, height: 9 * 1.05 * 4)
            coverTitle.alignment = .left
            cover.addSublayer(coverTitle)
            cover.setValue(-4 * CGFloat.pi / 180, forKeyPath: "transform.rotation.z")
            let format = HUDTextLayer(URL(fileURLWithPath: book.path).pathExtension.uppercased(),
                                      style: HUDTextStyle(size: 8.5, weight: .bold, color: HUDColor.onDarkTag))
            let tagWidth = ceil(format.preferredWidth) + 8
            let tag = HUDLayer.plain(); tag.frame = CGRect(x: width - 4 - tagWidth, y: wellHeight - 3 - 4 - 12, width: tagWidth, height: 12)
            tag.backgroundColor = HUDColor.rgb(0x141414, 0.75)
            format.lineFrame = CGRect(x: 4, y: 1.75, width: tagWidth - 8 + 1, height: 8.5)
            tag.addSublayer(format)
            let track = HUDLayer.plain(); track.frame = CGRect(x: 0, y: wellHeight - 3, width: width, height: 3); track.backgroundColor = HUDColor.black(0.25)
            let fill = HUDLayer.plain(); fill.frame = CGRect(x: 0, y: 0, width: width * CGFloat(min(1, max(0, book.progress))), height: 3)
            fill.backgroundColor = HUDColor.yellow; track.addSublayer(fill)
            [wellBrighten, codeBar, code, cover, tag, track].forEach(well.addSublayer)
            if missing {
                let veil = HUDLayer.plain("book.missing"); veil.frame = well.bounds; veil.backgroundColor = HUDColor.black(0.55)
                let text = HUDTextLayer("MISSING", style: HUDTextStyle(size: 10, weight: .bold, color: HUDColor.yellow, tracking: 0.1), alignment: .center)
                text.lineFrame = CGRect(x: 0, y: (wellHeight - 10) / 2, width: width, height: 10)
                veil.addSublayer(text); well.addSublayer(veil)
            }
            strip.frame = CGRect(x: 0, y: wellHeight, width: width, height: 24)
            labelText.lineFrame = CGRect(x: 6, y: 6.25, width: width - 12, height: 11.5)
            strip.addSublayer(labelText)
            more.layer.frame = CGRect(x: width - 4 - 22, y: 8, width: 22, height: 22); more.layer.zPosition = 2
            more.plate.frame = more.layer.bounds; more.plate.cornerRadius = 11; more.plate.backgroundColor = HUDColor.rgb(0x141414, 0.8)
            let dots = HUDTextLayer("⋯", style: HUDTextStyle(size: 12, weight: .black, color: HUDColor.white(1)), alignment: .center)
            dots.lineFrame = CGRect(x: 0, y: 5, width: 22, height: 12)
            more.layer.addSublayer(more.plate); more.layer.addSublayer(dots)
            brackets.frame = bounds; brackets.fillColor = HUDColor.yellow; brackets.opacity = 0
            brackets.path = HUDArchiveTile.bracketPath(bounds, inset: -5)
            [well, strip, more.layer, brackets].forEach(layer.addSublayer)
        }
        more.setTarget(more.layer.bounds, path: CGPath(ellipseIn: more.layer.bounds, transform: nil))
        renderSelection(animated: false)
        setTarget(bounds)
        env.applyScale(to: layer)
    }

    func setSelected(_ selected: Bool, animated: Bool = true) {
        guard selected != isSelected else { return }
        isSelected = selected
        renderSelection(animated: animated)
    }

    private func renderSelection(animated: Bool) {
        HUDMotion.animate(brackets, "opacity", to: Float(isSelected ? 1 : 0), duration: animated ? 0.1 : 0, timing: HUDEasing.ease, env: env)
        HUDMotion.animate(brackets, "path", to: HUDArchiveTile.bracketPath(layer.bounds, inset: isSelected ? -4 : -5), duration: animated ? 0.16 : 0, env: env)
        HUDMotion.perform {
            strip.backgroundColor = isSelected ? HUDColor.counterBackground : HUDColor.rgb(0xC4C4C6)
            labelText.color = isSelected ? HUDColor.yellow : HUDColor.nearBlack
        }
    }

    override func hoverChanged() {
        let on = isHovered && isEnabled
        HUDMotion.animate(wellBrighten, "opacity", to: Float(on ? 1 : 0), duration: HUDDuration.hover, timing: HUDEasing.ease, env: env)
        HUDMotion.animate(well, "transform.translation.y", to: on ? -2 : 0, duration: 0.25, env: env)
    }
}
