import AppKit
import QuartzCore

/// Clipboard (spec §3.8): the session's clipboard history as list cards under
/// type filter tabs. Capturing and restoring belong to ClipboardWatcher and
/// ClipboardStore; this canvas presents, copies, pins and removes records.
/// Pinned records lead the list (in capture order among themselves) and are
/// never deleted one by one; Clear and the store's limits keep them too.
final class ClipboardCanvas: HUDDesignCanvas {
    enum Filter: Int, CaseIterable {
        case all, text, link, image, file
        var title: String {
            switch self {
            case .all: return L10n.text("All", "全部")
            case .text: return L10n.text("Text", "文字")
            case .link: return L10n.text("Link", "链接")
            case .image: return L10n.text("Image", "图片")
            case .file: return L10n.text("File", "文件")
            }
        }
        func matches(_ item: ClipboardItem) -> Bool {
            switch self {
            case .all: return true
            case .text: return item.kind == .text
            case .link: return item.kind == .url
            case .image: return item.kind == .image
            case .file: return item.kind == .files
            }
        }
    }

    /// Restores a record to the clipboard; false when it could not be copied.
    var onCopy: ((UUID) -> Bool)?
    private let store: ClipboardStore
    private var observer: UUID?
    private(set) var filter: Filter = .all
    /// The keyboard's record (Return copies, Delete removes an unpinned one).
    private(set) var focusedID: UUID?
    private var cards: [(id: UUID, card: HUDListCard)] = []
    private weak var list: HUDScrollArea?
    /// What the page's layout depends on; anything else updates in place.
    private struct Structure: Equatable {
        let records: [UUID], pins: [Bool], visible: [UUID], capacity: Int, filter: Filter
    }
    private var renderedStructure: Structure?
    private var lastStatus: String?
    private var copying = false
    private var ageTimer: Timer?
    private let now: () -> Date

    init(context: HUDDesignContext, store: ClipboardStore, now: @escaping () -> Date = Date.init) {
        self.store = store; self.now = now
        super.init(module: .clipboard, context: context)
        lastStatus = store.statusMessage
        observer = store.observe { [weak self] in self?.storeChanged() }
    }

    deinit {
        ageTimer?.invalidate()
        if let observer { store.removeObserver(observer) }
    }

    // MARK: Lifecycle

    override func didActivate() {
        lastStatus = store.statusMessage
        if structure != renderedStructure { rerender() } else { refreshAges() }
        ageTimer?.invalidate()
        let timer = Timer(timeInterval: 30, repeats: true) { [weak self] _ in self?.refreshAges() }
        timer.tolerance = 5
        RunLoop.main.add(timer, forMode: .common)
        ageTimer = timer
    }

    override func didDeactivate() { ageTimer?.invalidate(); ageTimer = nil }
    override func willLeave() { didDeactivate() }

    override func recordingModeChanged() {
        super.recordingModeChanged()
        if context.recordingMode, root.superlayer != nil { HUDRedaction.playEntrance(in: [page.layer], env: env) }
    }

    private var structure: Structure {
        let records = Self.ordered(store.items)
        return Structure(records: records.map(\.id), pins: records.map(\.isPinned),
                         visible: records.filter(filter.matches).map(\.id), capacity: store.capacity, filter: filter)
    }

    /// Pinned records first; each group keeps the store's order (newest
    /// capture first), so unpinning returns a record to its place.
    static func ordered(_ items: [ClipboardItem]) -> [ClipboardItem] { items.filter(\.isPinned) + items.filter { !$0.isPinned } }

    /// The records under the current filter, in list order.
    private var visibleItems: [ClipboardItem] { Self.ordered(store.items).filter(filter.matches) }

    /// A hidden canvas rebuilds when it is next shown, not on every capture.
    override func makeContent(for module: HUDModule, style: HUDModuleContentStyle) -> CALayer {
        if renderedLanguage != nil, structure != renderedStructure { rerender() }
        return super.makeContent(for: module, style: style)
    }

    private func storeChanged() {
        if let focused = focusedID, !store.items.contains(where: { $0.id == focused }) { focusedID = nil }
        if isActive, structure != renderedStructure {
            let before = Dictionary(cards.map { ($0.id, $0.card.layer.frame.minY) }, uniquingKeysWith: { a, _ in a })
            rerender()
            // A pin or unpin slides the cards that moved to their new rows.
            for (id, card) in cards {
                guard let y = before[id], y != card.layer.frame.minY else { continue }
                HUDMotion.perform { card.layer.setValue(y - card.layer.frame.minY, forKeyPath: "transform.translation.y") }
                HUDMotion.animate(card.layer, "transform.translation.y", to: 0, duration: 0.3, env: env)
            }
        }
        let status = store.statusMessage
        defer { lastStatus = status }
        guard isActive, !copying, let status, status != lastStatus else { return }
        toast(icon: .warn, "Notice", "提示", detail: status)
    }

    // MARK: Page

    override func buildPage(_ page: HUDDesignPage) {
        renderedStructure = structure
        cards = []
        let records = store.items, visible = visibleItems
        let clear = HUDButton(id: "clipboard.clear", title: L10n.text("Clear", "清空"), icon: .trash, kind: .secondary, size: .small, env: env)
        clear.place(at: .zero)
        clear.isEnabled = records.contains { !$0.isPinned }
        clear.onActivate = { [weak self] in self?.confirmClear() }
        header.configure(crumbs: ["CLIPBOARD"], code: "MOD-03", title: HUDModuleTitle("Clipboard", "剪贴板缓存"),
                         counter: HUDFormat.pad2(records.count) + "/" + HUDFormat.pad2(store.capacity), showsBack: false, actions: [clear])

        let tabs = HUDTabs(id: "clipboard.filter", titles: Filter.allCases.map { ($0.title, nil) }, selectedIndex: filter.rawValue,
                           width: HUDMetric.box.width, env: env)
        tabs.onSelect = { [weak self] index, direction in
            guard let self, let next = Filter(rawValue: index) else { return }
            self.filter = next
            self.navigate(dx: direction)
        }
        HUDMotion.perform { tabs.layer.frame.origin = CGPoint(x: 0, y: HUDMetric.headerHeight); page.layer.addSublayer(tabs.layer) }
        page.tabs = tabs
        page.add(tabs.controls)
        page.stagger(tabs.layer)

        // `.rows.scroll`: at most 350 tall, 58 px cards 6 apart, 6 px right padding.
        let top = HUDMetric.headerHeight + 34 + 10
        let pitch = HUDListCard.height + 6
        let contentHeight = visible.isEmpty ? 250 : CGFloat(visible.count) * pitch - 6
        let height = min(350, contentHeight)
        let scroll = page.scrollArea(id: "clipboard.rows", frame: CGRect(x: 0, y: top, width: HUDMetric.box.width, height: height))
        list = scroll
        let width = HUDMetric.box.width - 6 - (contentHeight > height ? 4 : 0)
        if visible.isEmpty {
            let empty = HUDEmptyState.bilingual("NO RECORDS", "剪贴板为空", size: CGSize(width: width, height: 250), env: env)
            HUDMotion.perform { empty.frame.origin = .zero; scroll.content.addSublayer(empty) }
            page.stagger(empty)
        }
        let date = now()
        for (index, item) in visible.enumerated() {
            // Recording mode keeps the title from VoiceOver too.
            let spoken = context.recordingMode ? L10n.text("Redacted", "已隐藏") : Self.title(item)
            // Row actions (light 24 px icon buttons): pin is lit while pinned; delete is off for a pinned record.
            let pin = HUDIconButton(id: "clip:\(index):pin", label: (item.isPinned ? L10n.text("Unpin: ", "取消固定：") : L10n.text("Pin: ", "固定："))
                                    + spoken, icon: .pin, style: .light, side: 24, iconSize: 13, env: env)
            pin.isOn = item.isPinned
            pin.onActivate = { [weak self] in self?.togglePin(item.id) }
            let delete = HUDIconButton(id: "clip:\(index):delete", label: L10n.text("Delete: ", "删除：") + spoken, icon: .trash,
                                       style: .light, side: 24, iconSize: 13, env: env)
            delete.isEnabled = !item.isPinned
            delete.onActivate = { [weak self] in self?.remove(item.id) }
            let card = HUDListCard(id: "clip:\(index)", kind: Self.cardKind(item.kind), title: Self.title(item), meta: Self.meta(item),
                                   age: Self.age(of: item.createdAt, now: date), pinned: item.isPinned, width: width,
                                   thumbnail: context.recordingMode ? nil : item.thumbnail, copyTitle: L10n.text("COPY", "复制"),
                                   redacted: context.recordingMode, actions: [pin, delete], env: env)
            card.label = L10n.text("Copy: ", "复制：") + spoken
            card.isFocused = item.id == focusedID
            card.onActivate = { [weak self] in self?.copy(item.id) }
            HUDMotion.perform { card.layer.frame.origin = CGPoint(x: 0, y: CGFloat(index) * pitch); scroll.content.addSublayer(card.layer) }
            page.add(card.controls)
            page.stagger(card.layer)
            cards.append((item.id, card))
        }
        scroll.setContentHeight(contentHeight)
        page.restoreOffset(of: scroll)
    }

    private func refreshAges() {
        let date = now()
        for (id, card) in cards {
            guard let item = store.items.first(where: { $0.id == id }) else { continue }
            card.setAge(Self.age(of: item.createdAt, now: date))
        }
    }

    // MARK: Actions

    func copy(_ id: UUID) {
        guard store.items.contains(where: { $0.id == id }) else { return }
        setFocus(id)
        copying = true
        let success = onCopy?(id) ?? false
        copying = false
        lastStatus = store.statusMessage
        if success {
            cards.first { $0.id == id }?.card.flashCopied()
            toast(icon: .copy, "Copied", "已复制到剪贴板")
        } else {
            toast(icon: .warn, "Notice", "提示", detail: store.statusMessage ?? L10n.text("Could not restore this item", "无法恢复此项目"))
        }
    }

    func togglePin(_ id: UUID) { store.togglePin(id: id) }

    /// Deletes one unpinned record (pinned records are protected).
    func remove(_ id: UUID) {
        guard store.items.first(where: { $0.id == id })?.isPinned == false else { return }
        let visible = visibleItems
        if focusedID == id, let index = visible.firstIndex(where: { $0.id == id }) {
            let rest = visible.filter { $0.id != id }
            focusedID = rest.isEmpty ? nil : rest[min(index, rest.count - 1)].id
        }
        store.remove(id: id)
    }

    private func confirmClear() {
        let removable = store.items.filter { !$0.isPinned }.count, pinned = store.items.count - removable
        guard removable > 0 else { return }
        let message = pinned > 0 ? L10n.text("\(removable) records / \(pinned) pinned kept", "\(removable) 条记录 / 保留 \(pinned) 条固定记录")
                                 : L10n.text("\(removable) records", "\(removable) 条记录")
        confirm(HUDConfirmRequest(id: "clipboard.clear", title: L10n.text("Clear clipboard history?", "清空剪贴板记录？"), message: message,
                                  confirmTitle: L10n.text("Clear", "清空"), danger: true, icon: .trash)) { [weak self] in
            guard let self, self.store.clearUnpinned() else { return }
            self.toast(icon: .trash, "History cleared", "剪贴板记录已清空")
        }
    }

    /// Selects a filter tab the way the tab bar does (verification and tests).
    func selectFilter(_ next: Filter) {
        guard next != filter else { return }
        let direction: CGFloat = next.rawValue > filter.rawValue ? 1 : -1
        filter = next
        navigate(dx: direction)
    }

    // MARK: Keyboard and context menu

    override func handleKey(_ event: NSEvent) -> Bool {
        let flags = event.modifierFlags.intersection([.command, .control, .option, .shift])
        guard flags.isEmpty else { return false }
        switch event.keyCode {
        case 125: moveFocus(1); return true
        case 126: moveFocus(-1); return true
        case 36, 76: if let id = focusedID { copy(id) }; return true
        case 51, 117: if let id = focusedID { remove(id) }; return true
        default:
            guard let character = event.charactersIgnoringModifiers, let number = Int(character), (1...6).contains(number) else { return false }
            copyVisible(at: number - 1)
            return true
        }
    }

    func moveFocus(_ step: Int) {
        let visible = visibleItems
        guard !visible.isEmpty else { return }
        let current = focusedID.flatMap { id in visible.firstIndex { $0.id == id } }
        let index = min(visible.count - 1, max(0, current.map { $0 + step } ?? (step < 0 ? visible.count - 1 : 0)))
        setFocus(visible[index].id)
        if let list, let card = cards.first(where: { $0.id == focusedID })?.card { list.reveal(card.layer.frame) }
    }

    /// 1–6 copy the records currently in view, top to bottom.
    func copyVisible(at index: Int) {
        guard let list else { return }
        let viewport = CGRect(x: 0, y: list.offset, width: list.layer.bounds.width, height: list.layer.bounds.height)
        let shown = cards.filter { viewport.intersects($0.card.layer.frame) }
        guard shown.indices.contains(index) else { return }
        copy(shown[index].id)
    }

    private func setFocus(_ id: UUID?) {
        focusedID = id
        for (cardID, card) in cards { card.isFocused = cardID == id }
    }

    override func handleRightPointerDown(at point: CGPoint) -> Bool {
        guard let id = cards.first(where: { entry in
            entry.card.layer.isVisible(in: root) && entry.card.layer.isUnclipped(point, in: root)
                && entry.card.layer.bounds.contains(entry.card.layer.convert(point, from: root))
        })?.id, let item = store.items.first(where: { $0.id == id }) else { return false }
        setFocus(id)
        let items = [HUDMenu.Item(id: "copy", title: L10n.text("Copy", "复制")),
                     HUDMenu.Item(id: "pin", title: item.isPinned ? L10n.text("Unpin", "取消固定") : L10n.text("Pin", "固定")),
                     HUDMenu.Item(id: "delete", title: L10n.text("Delete", "删除"), danger: true, separatorBefore: true, enabled: !item.isPinned)]
        openMenu(id: "clipboard.menu", items: items, anchorRect: CGRect(x: point.x, y: point.y, width: 1, height: 1), minimumWidth: 150) { [weak self] pick in
            switch pick.id {
            case "copy": self?.copy(id)
            case "pin": self?.togglePin(id)
            default: self?.remove(id)
            }
        }
        return true
    }

    // MARK: Formatting (spec §2.14, §3.8)

    static func cardKind(_ kind: ClipboardKind) -> HUDListCard.Kind {
        switch kind {
        case .text: return .text
        case .url: return .link
        case .image: return .image
        case .files: return .file
        }
    }

    static func title(_ item: ClipboardItem) -> String { item.kind == .image ? L10n.text("Image", "图片") : item.preview }

    /// The meta line keeps its triple spaces: `Notes   28 chars`, `Safari`,
    /// `PNG   2880 × 1872`, `Finder   48.2 MB`.
    static func meta(_ item: ClipboardItem) -> String {
        let details = item.details
        var parts: [String?]
        switch item.kind {
        case .text: parts = [details.source, details.characters.map { L10n.text("\($0) chars", "\($0) 字符") }]
        case .url: parts = [details.source ?? URL(string: item.preview.trimmingCharacters(in: .whitespacesAndNewlines))?.host]
        case .image:
            let size = details.pixelWidth.flatMap { width in details.pixelHeight.map { "\(width) × \($0)" } }
            parts = [details.format, size]
        case .files:
            let extent: String?
            if let count = details.fileCount, count > 1 { extent = L10n.text("\(count) items", "\(count) 项") }
            else if let bytes = details.byteCount { extent = ByteCountFormatter.string(fromByteCount: bytes, countStyle: .file) }
            else { extent = L10n.text("Folder", "文件夹") }
            parts = [details.source, extent]
        }
        return parts.compactMap { $0 }.filter { !$0.isEmpty }.joined(separator: "   ")
    }

    /// `NOW`, `2 MIN`, `1 H`, `3 D`.
    static func age(of date: Date, now: Date) -> String {
        let seconds = max(0, Int(now.timeIntervalSince(date)))
        if seconds < 60 { return L10n.text("NOW", "刚刚") }
        let minutes = seconds / 60
        if minutes < 60 { return L10n.text("\(minutes) MIN", "\(minutes) 分钟") }
        let hours = minutes / 60
        if hours < 24 { return L10n.text("\(hours) H", "\(hours) 小时") }
        return L10n.text("\(hours / 24) D", "\(hours / 24) 天")
    }

    // MARK: Testing

    var cardTitlesForTesting: [String] { cards.map(\.card.label) }
    var focusedCardIndexForTesting: Int? { cards.firstIndex { $0.card.isFocused } }
}
