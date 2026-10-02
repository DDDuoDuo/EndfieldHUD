import AppKit
import QuartzCore
import UniformTypeIdentifiers

struct ShelfCanvasAction {
    let id: String
    let label: String
    let rect: CGRect
}

/// A retained, paged collection inside the projected HUD. Only bookmarks and
/// metadata belong to the shelf; card commands never move or delete Finder items.
final class FileShelfCanvas: NSObject, HUDModuleContentFactory {
    let layer = CALayer()
    var onChange: (() -> Void)?
    var onChooseFiles: (() -> Void)?
    var onPreview: ((UUID) -> Void)?
    var onReveal: ((UUID) -> Void)?
    var onItemsAdded: (([String]) -> Void)?
    var onItemRemoved: ((String) -> Void)?
    var onShelfCleared: ((Int) -> Void)?
    private(set) var selectedID: UUID?
    private(set) var selectedIDs: Set<UUID> = []
    private var selectionAnchorID: UUID?
    private var pendingSingleSelection: UUID?
    private(set) var pageIndex = 0
    var itemCount: Int { items.count }
    var accessibilityStatus: String? { errorMessage }
    var pageCount: Int { max(1, (items.count + Self.pageCapacity - 1) / Self.pageCapacity) }
    var selectedRow: ShelfItem? { selectedID.flatMap { id in items.first { $0.id == id } } }
    var accessibleActions: [ShelfCanvasAction] {
        var result = toolbarActions()
        for item in visibleItems {
            guard let rect = cardRect(for: item.id) else { continue }
            let details = [item.name, typeLabel(item), sizeLabel(item), item.availabilityError == nil ? nil : L10n.text("Unavailable", "不可用")].compactMap { $0 }.joined(separator: ", ")
            result.append(ShelfCanvasAction(id: actionID(item.id, "select"), label: details, rect: rect))
            result.append(contentsOf: cardActions(item))
        }
        return result
    }

    private struct IconEntry {
        let path: String
        let unavailable: Bool
        let image: NSImage
        let cgImage: CGImage?
    }
    private static let pageCapacity = 6
    private static let contentRect = CGRect(x: 9, y: 40, width: 382, height: 248)
    private let store: FileShelfStore?
    private var items: [ShelfItem]
    private var icons: [UUID: IconEntry] = [:]
    private var errorMessage: String?
    private var confirmingClear = false
    private var dropTarget = false
    private var scrollAccumulation: CGFloat = 0
    private var dark = true
    private var scale: CGFloat = 2
    private var yellow: NSColor { HUDRuntimeAppearance.accent }
    private let heading = CATextLayer()
    private let status = CATextLayer()
    private let collection = CALayer()
    private let dropOutline = CAShapeLayer()
    private let toolbar = CALayer()
    private let reduceMotion: () -> Bool
    private var active = false
    // A hidden shelf does not resolve bookmarks or ask Finder for file icons.
    // Keep its semantic state available before the first presentation.
    private var artworkEnabled = false
    private var presentationPrepared = false
    private var pageTransition: HUDSubsectionTransition!
    private var primary: NSColor { NSColor(white: dark ? 0.94 : 0.11, alpha: 1) }
    private var muted: NSColor { NSColor(white: dark ? 0.68 : 0.38, alpha: 1) }
    private var cardInk: NSColor { NSColor(white: 0.14, alpha: 1) }
    private var visibleItems: ArraySlice<ShelfItem> {
        let start = min(items.count, pageIndex * Self.pageCapacity)
        return items[start..<min(items.count, start + Self.pageCapacity)]
    }

    init(store: FileShelfStore?, error: String? = nil,
         reduceMotion: @escaping () -> Bool = { HUDRuntimeAppearance.reduceMotion }) {
        self.store = store
        self.reduceMotion = reduceMotion
        items = store?.items ?? []
        errorMessage = error
        super.init()
        withoutActions {
            layer.name = "module.fileShelf.canvas"
            layer.frame = CGRect(x: 0, y: 0, width: 400, height: 334)
            layer.allowsGroupOpacity = false
            collection.frame = layer.bounds
            collection.allowsGroupOpacity = false
            layer.addSublayer(collection)
            dropOutline.frame = Self.contentRect
            dropOutline.fillColor = nil
            dropOutline.lineWidth = 1.5
            dropOutline.path = CGPath(roundedRect: CGRect(origin: .zero, size: Self.contentRect.size), cornerWidth: 5, cornerHeight: 5, transform: nil)
            layer.addSublayer(dropOutline)
            heading.frame = CGRect(x: 11, y: 0, width: 376, height: 20)
            heading.font = NSFont.systemFont(ofSize: 15, weight: .semibold)
            heading.fontSize = 15
            layer.addSublayer(heading)
            status.frame = CGRect(x: 12, y: 22, width: 376, height: 13)
            status.font = NSFont.systemFont(ofSize: 9.5)
            status.fontSize = 9.5
            status.truncationMode = .end
            layer.addSublayer(status)
            toolbar.frame = layer.bounds
            layer.addSublayer(toolbar)
        }
        pageTransition = HUDSubsectionTransition(content: collection, viewport: Self.contentRect)
    }

    func makeContent(for module: HUDModule, style: HUDModuleContentStyle) -> CALayer {
        dark = style.dark
        scale = style.contentsScale
        artworkEnabled = true
        if presentationPrepared { withoutActions { repaint() } }
        else { preparePresentation() }
        return layer
    }

    func updateRenderScale(_ value: CGFloat) {
        if reduceMotion() { pageTransition.settle(); settleAnimations(in: layer) }
        let next = value.isFinite ? min(8, max(1, value)) : 2
        guard next != scale else { return }
        scale = next
        withoutActions { repaint() }
    }

    func activate() {
        active = true; artworkEnabled = true
        let previousError = errorMessage
        do { try store?.refresh() }
        catch { errorMessage = error.localizedDescription }
        if !presentationPrepared || items != (store?.items ?? []) || errorMessage != previousError {
            presentationPrepared = true
            refreshFromStore()
        }
    }

    /// Resolve and draw before the module reveal. Enabling input afterwards
    /// must not replace the cards that just completed their arrival.
    private func preparePresentation() {
        presentationPrepared = true
        do { try store?.refresh() }
        catch { errorMessage = error.localizedDescription }
        refreshFromStore()
    }

    func deactivate() {
        active = false; artworkEnabled = false; pageTransition.settle(); settleAnimations(in: layer)
        presentationPrepared = false
        pendingSingleSelection = nil
        scrollAccumulation = 0
        guard confirmingClear || dropTarget else { return }
        confirmingClear = false
        dropTarget = false
    }

    func refreshFromStore() {
        items = store?.items ?? []
        let retained = Set(items.map(\.id))
        icons = icons.filter { retained.contains($0.key) }
        selectedIDs.formIntersection(retained)
        if let id = selectedID, !retained.contains(id) { selectedID = nil }
        if selectedID == nil { selectedID = items.first { selectedIDs.contains($0.id) }?.id }
        if let anchor = selectionAnchorID, !retained.contains(anchor) { selectionAnchorID = selectedID }
        pendingSingleSelection = nil
        pageIndex = min(pageIndex, pageCount - 1)
        if items.isEmpty { confirmingClear = false }
        withoutActions { repaint() }
        onChange?()
    }

    @discardableResult
    func importURLs(_ urls: [URL]) -> Bool {
        guard let store = store else { reportUnavailable(); return false }
        guard !urls.isEmpty else { return false }
        scrollAccumulation = 0
        do {
            let previousIDs = Set(store.items.map(\.id))
            let count = try store.add(urls: urls)
            errorMessage = nil
            confirmingClear = false
            if count > 0 {
                selectedID = store.items.last?.id
                selectedIDs = Set(store.items.filter { !previousIDs.contains($0.id) }.map(\.id))
                selectionAnchorID = selectedID
                pageIndex = max(0, (store.items.count - 1) / Self.pageCapacity)
            }
            refreshFromStore()
            let addedNames = store.items.filter { !previousIDs.contains($0.id) }.map(\.name)
            if !addedNames.isEmpty {
                pageTransition.reveal(direction: 1, animated: active && !reduceMotion())
                onItemsAdded?(addedNames)
            }
            return true // Existing references are valid drops, too.
        } catch {
            errorMessage = error.localizedDescription
            refreshFromStore() // Failed batches leave the store's committed references intact.
            return false
        }
    }

    @discardableResult
    func mouseDown(at point: CGPoint, clickCount: Int, modifiers: NSEvent.ModifierFlags = []) -> Bool {
        guard point.x.isFinite, point.y.isFinite, layer.bounds.contains(point) else { return false }
        prepareArtworkForInteraction()
        pendingSingleSelection = nil
        if let action = toolbarActions().first(where: { $0.rect.contains(point) }) {
            perform(actionID: action.id)
            return true
        }
        for item in visibleItems {
            guard let rect = cardRect(for: item.id), rect.contains(point) else { continue }
            if let action = cardActions(item).first(where: { $0.rect.contains(point) }) {
                perform(actionID: action.id)
                return true
            }
            select(item.id, modifiers: modifiers, preserveForDrag: clickCount == 1)
            if clickCount >= 2 && item.availabilityError == nil { onPreview?(item.id) }
            return true
        }
        if Self.contentRect.contains(point) { select(nil) }
        return true
    }

    /// Only card bodies initiate native drags. Inline controls and unavailable
    /// references are deliberately excluded, including disabled control slots.
    func itemAt(point: CGPoint) -> UUID? {
        guard point.x.isFinite, point.y.isFinite else { return nil }
        for item in visibleItems where item.availabilityError == nil {
            guard let rect = cardRect(for: item.id), rect.contains(point),
                  !controlStrip(in: rect).contains(point) else { continue }
            return item.id
        }
        return nil
    }

    func cardRect(for id: UUID) -> CGRect? {
        guard let index = items.firstIndex(where: { $0.id == id }),
              index / Self.pageCapacity == pageIndex else { return nil }
        let local = index % Self.pageCapacity
        return CGRect(x: 12 + CGFloat(local % 2) * 192, y: 44 + CGFloat(local / 2) * 80, width: 184, height: 74)
    }

    func icon(for id: UUID) -> NSImage? {
        guard let item = items.first(where: { $0.id == id }) else { return nil }
        return cachedIcon(item).image
    }

    /// Plain mouse-down on one member of a selected group keeps the group alive
    /// until we know whether this is a click or a native drag, as Finder does.
    func finishPointerSelection() {
        guard let id = pendingSingleSelection else { return }
        pendingSingleSelection = nil
        select(id)
    }

    func beginSelectionDrag() { pendingSingleSelection = nil }

    /// Stable shelf order gives the native drag one NSURL writer per object.
    /// Unavailable cards may be selected for deletion but cannot be exported.
    func dragSelection(primaryID: UUID) -> [UUID] {
        let wanted = selectedIDs.contains(primaryID) ? selectedIDs : [primaryID]
        return items.filter { wanted.contains($0.id) && $0.availabilityError == nil }.map(\.id)
    }

    @discardableResult
    func scroll(at point: CGPoint, delta: CGFloat) -> Bool {
        guard Self.contentRect.contains(point), delta.isFinite, abs(delta) > 0.01 else { return false }
        if (delta > 0) != (scrollAccumulation > 0) { scrollAccumulation = 0 }
        scrollAccumulation += delta
        if abs(scrollAccumulation) >= 40 {
            changePage(by: scrollAccumulation > 0 ? 1 : -1)
            scrollAccumulation = 0
        }
        return true
    }

    func setDropTarget(_ value: Bool) {
        guard dropTarget != value else { return }
        dropTarget = value
        withoutActions { updateStatus() }
        if active && !reduceMotion(), value {
            let trace = CABasicAnimation(keyPath: "strokeEnd")
            trace.fromValue = 0; trace.toValue = 1; trace.duration = 0.20
            trace.timingFunction = CAMediaTimingFunction(name: .easeOut)
            dropOutline.add(trace, forKey: "shelf.drop.trace")
        } else { dropOutline.removeAnimation(forKey: "shelf.drop.trace") }
    }

    func showError(_ message: String) {
        errorMessage = message
        withoutActions { updateStatus() }
        onChange?()
    }

    func deleteSelection() {
        let ids = items.filter { selectedIDs.contains($0.id) }.map(\.id)
        guard !ids.isEmpty else { return }
        remove(ids)
    }

    func previewSelection() {
        guard let item = selectedRow, item.availabilityError == nil else { return }
        onPreview?(item.id)
    }

    func revealSelection() {
        guard let item = selectedRow, item.availabilityError == nil else { return }
        onReveal?(item.id)
    }

    func perform(actionID value: String) {
        prepareArtworkForInteraction()
        switch value {
        case "shelf:add":
            guard store != nil else { reportUnavailable(); return }
            onChooseFiles?()
        case "shelf:clear":
            guard !items.isEmpty else { return }
            confirmingClear = true
            withoutActions { renderToolbar() }
            animateToolbar()
            onChange?()
        case "shelf:cancelClear":
            confirmingClear = false
            withoutActions { renderToolbar() }
            animateToolbar()
            onChange?()
        case "shelf:confirmClear":
            guard confirmingClear, let store = store else { return }
            do {
                let count = store.items.count
                try store.clear(); errorMessage = nil; selectedID = nil; selectedIDs = []; selectionAnchorID = nil
                confirmingClear = false; refreshFromStore()
                pageTransition.reveal(direction: -1, animated: active && !reduceMotion())
                if count > 0 { onShelfCleared?(count) }
            }
            catch { showError(error.localizedDescription) }
        case "shelf:previous": changePage(by: -1)
        case "shelf:next": changePage(by: 1)
        default:
            let parts = value.split(separator: ":")
            guard parts.count == 3, parts[0] == "shelf", let id = UUID(uuidString: String(parts[1])),
                  let item = items.first(where: { $0.id == id }) else { return }
            switch parts[2] {
            case "select":
                if let index = items.firstIndex(where: { $0.id == id }) { pageIndex = index / Self.pageCapacity }
                select(id)
            case "preview":
                guard item.availabilityError == nil else { return }
                select(id); onPreview?(id)
            case "reveal":
                guard item.availabilityError == nil else { return }
                select(id); onReveal?(id)
            case "remove": remove(id)
            default: break
            }
        }
    }

    private func select(_ id: UUID?, modifiers: NSEvent.ModifierFlags = [], preserveForDrag: Bool = false) {
        let previous = selectedIDs
        let previousPrimary = selectedID
        let wasConfirmingClear = confirmingClear
        if let id {
            if modifiers.contains(.shift), let anchor = selectionAnchorID,
               let start = items.firstIndex(where: { $0.id == anchor }),
               let end = items.firstIndex(where: { $0.id == id }) {
                selectedIDs = Set(items[min(start, end)...max(start, end)].map(\.id))
                selectedID = id
            } else if modifiers.contains(.command) {
                if selectedIDs.contains(id) { selectedIDs.remove(id) }
                else { selectedIDs.insert(id) }
                selectedID = selectedIDs.contains(id) ? id : items.first { selectedIDs.contains($0.id) }?.id
                selectionAnchorID = selectedID
            } else if preserveForDrag, selectedIDs.contains(id), selectedIDs.count > 1 {
                selectedID = id
                pendingSingleSelection = id
            } else {
                selectedIDs = [id]; selectedID = id; selectionAnchorID = id
            }
        } else {
            selectedIDs = []; selectedID = nil; selectionAnchorID = nil
        }
        confirmingClear = false
        guard previous != selectedIDs || previousPrimary != selectedID || wasConfirmingClear else { return }
        withoutActions { repaint() }
        animateSelection(previous: previous)
        onChange?()
    }

    private func changePage(by direction: Int) {
        scrollAccumulation = 0
        let next = min(pageCount - 1, max(0, pageIndex + direction))
        guard pageIndex != next else { return }
        pageIndex = next
        selectedID = nil; selectedIDs = []; selectionAnchorID = nil; pendingSingleSelection = nil
        confirmingClear = false
        withoutActions { repaint() }
        pageTransition.reveal(direction: CGFloat(direction), animated: active && !reduceMotion())
        onChange?()
    }

    private func remove(_ id: UUID) {
        remove([id])
    }

    private func remove(_ ids: [UUID]) {
        guard let store = store else { reportUnavailable(); return }
        do {
            for id in ids {
                let name = store.items.first { $0.id == id }?.name
                try store.remove(id: id)
                if let name { onItemRemoved?(name) }
            }
            errorMessage = nil; refreshFromStore()
            pageTransition.reveal(direction: -1, animated: active && !reduceMotion())
        }
        catch { refreshFromStore(); showError(error.localizedDescription) }
    }

    private func toolbarActions() -> [ShelfCanvasAction] {
        if confirmingClear {
            return [ShelfCanvasAction(id: "shelf:cancelClear", label: L10n.text("Cancel", "取消"), rect: CGRect(x: 218, y: 299, width: 67, height: 27)),
                    ShelfCanvasAction(id: "shelf:confirmClear", label: L10n.text("Clear shelf", "清空暂存架"), rect: CGRect(x: 294, y: 299, width: 94, height: 27))]
        }
        var result = [ShelfCanvasAction(id: "shelf:add", label: L10n.text("Add files", "添加文件"), rect: CGRect(x: 12, y: 299, width: 84, height: 27))]
        if !items.isEmpty { result.append(ShelfCanvasAction(id: "shelf:clear", label: L10n.text("Clear all", "清空"), rect: CGRect(x: 104, y: 299, width: 74, height: 27))) }
        if pageIndex > 0 { result.append(ShelfCanvasAction(id: "shelf:previous", label: L10n.text("Previous page", "上一页"), rect: CGRect(x: 265, y: 299, width: 29, height: 27))) }
        if pageIndex + 1 < pageCount { result.append(ShelfCanvasAction(id: "shelf:next", label: L10n.text("Next page", "下一页"), rect: CGRect(x: 359, y: 299, width: 29, height: 27))) }
        return result
    }

    private func cardActions(_ item: ShelfItem) -> [ShelfCanvasAction] {
        guard let card = cardRect(for: item.id) else { return [] }
        var result: [ShelfCanvasAction] = []
        if item.availabilityError == nil {
            result.append(ShelfCanvasAction(id: actionID(item.id, "preview"), label: L10n.text("Quick Look: ", "快速查看：") + item.name,
                                           rect: CGRect(x: card.minX + 113, y: card.minY + 49, width: 20, height: 20)))
            result.append(ShelfCanvasAction(id: actionID(item.id, "reveal"), label: L10n.text("Reveal in Finder: ", "在访达中显示：") + item.name,
                                           rect: CGRect(x: card.minX + 136, y: card.minY + 49, width: 20, height: 20)))
        }
        result.append(ShelfCanvasAction(id: actionID(item.id, "remove"), label: L10n.text("Remove from shelf: ", "从暂存架移除：") + item.name,
                                       rect: CGRect(x: card.minX + 159, y: card.minY + 49, width: 20, height: 20)))
        return result
    }

    private func controlStrip(in rect: CGRect) -> CGRect { CGRect(x: rect.minX + 111, y: rect.minY + 47, width: 71, height: 26) }
    private func actionID(_ id: UUID, _ verb: String) -> String { "shelf:\(id.uuidString):\(verb)" }
    private func sizeLabel(_ item: ShelfItem) -> String {
        guard !item.isDirectory, let bytes = item.byteCount else { return "—" }
        return ByteCountFormatter.string(fromByteCount: bytes, countStyle: .file)
    }
    private func typeLabel(_ item: ShelfItem) -> String {
        let ext = URL(fileURLWithPath: item.lastKnownPath).pathExtension.lowercased()
        if item.isDirectory {
            // Finder packages are directory-backed documents or applications.
            if ["app", "bundle", "framework", "plugin", "pages", "numbers", "key", "rtfd", "playground", "xcodeproj", "xcworkspace"].contains(ext), !item.typeDescription.isEmpty {
                return item.typeDescription
            }
            return L10n.text("Folder", "文件夹")
        }
        switch ext {
        case "jpg", "jpeg", "png", "heic", "heif", "gif", "tif", "tiff", "bmp", "webp", "svg": return L10n.text("Image", "图像") + " · " + ext.uppercased()
        case "pdf": return "PDF"
        case "mov", "mp4", "m4v", "avi", "mkv", "webm": return L10n.text("Video", "视频") + " · " + ext.uppercased()
        case "zip", "rar", "7z", "tar", "gz", "bz2", "xz", "tgz": return L10n.text("Archive", "压缩文件") + " · " + ext.uppercased()
        default: return item.typeDescription.isEmpty ? L10n.text("File", "文件") : item.typeDescription
        }
    }

    private func cachedIcon(_ item: ShelfItem) -> IconEntry {
        let unavailable = item.availabilityError != nil
        if let existing = icons[item.id], existing.path == item.lastKnownPath, existing.unavailable == unavailable { return existing }
        let image: NSImage
        if !unavailable, let access = try? store?.access(id: item.id) {
            image = NSWorkspace.shared.icon(forFile: access.url.path)
            var proposed = CGRect(x: 0, y: 0, width: 64, height: 64)
            let cgImage = image.cgImage(forProposedRect: &proposed, context: nil, hints: nil)
            access.close()
            let result = IconEntry(path: item.lastKnownPath, unavailable: unavailable, image: image, cgImage: cgImage)
            icons[item.id] = result
            return result
        }
        if #available(macOS 11.0, *) {
            let type: UTType = item.isDirectory ? .folder : (UTType(filenameExtension: URL(fileURLWithPath: item.lastKnownPath).pathExtension) ?? .data)
            image = NSWorkspace.shared.icon(for: type)
        } else {
            image = NSWorkspace.shared.icon(forFileType: item.isDirectory ? "public.folder" : URL(fileURLWithPath: item.lastKnownPath).pathExtension)
        }
        var proposed = CGRect(x: 0, y: 0, width: 64, height: 64)
        let result = IconEntry(path: item.lastKnownPath, unavailable: unavailable, image: image,
                               cgImage: image.cgImage(forProposedRect: &proposed, context: nil, hints: nil))
        icons[item.id] = result
        return result
    }

    private func repaint() {
        guard artworkEnabled else { return }
        heading.string = HUDModule.fileShelf.title
        heading.foregroundColor = primary.cgColor
        heading.contentsScale = HUDRenderScale.contentScale(for: heading, baseScale: scale)
        status.contentsScale = HUDRenderScale.contentScale(for: status, baseScale: scale)
        collection.sublayers?.forEach { $0.removeFromSuperlayer() }
        if items.isEmpty {
            let symbol = CALayer()
            symbol.frame = CGRect(x: 165, y: 88, width: 70, height: 56)
            collection.addSublayer(symbol)
            if !EndfieldGameIcon.depot.add(to: symbol, rect: CGRect(x: 7, y: 0, width: 56, height: 56),
                                          tint: muted, contentsScale: scale) {
                drawFolder(in: CGRect(x: 4, y: 4, width: 62, height: 42), color: muted, parent: symbol, lineWidth: 1.8)
            }
            addText(L10n.text("Drop files or folders here", "将文件或文件夹拖放到此处"), rect: CGRect(x: 28, y: 164, width: 344, height: 22), size: 14, color: primary, parent: collection, weight: .medium, alignment: .center)
            addText(L10n.text("Keep references. Drag them out whenever you need.", "仅保留引用，可随时拖出使用。"), rect: CGRect(x: 24, y: 194, width: 352, height: 38), size: 10.5, color: muted, parent: collection, alignment: .center, wrapped: true)
        } else {
            for item in visibleItems { render(item) }
        }
        updateStatus()
        renderToolbar()
    }

    private func prepareArtworkForInteraction() {
        guard !artworkEnabled else { return }
        artworkEnabled = true
        withoutActions { repaint() }
    }

    private func updateStatus() {
        dropOutline.strokeColor = dropTarget ? yellow.cgColor : NSColor.clear.cgColor
        if dropTarget {
            status.string = L10n.text("Release to keep file references", "松开以暂存文件引用")
            status.foregroundColor = (dark ? yellow : yellow.blended(withFraction: 0.40, of: .black) ?? yellow).cgColor
        } else if let message = errorMessage {
            status.string = message
            status.foregroundColor = NSColor.systemRed.cgColor
        } else {
            status.string = items.isEmpty ? L10n.text("Files stay in their original locations", "文件保留在原位置")
                : (selectedIDs.count > 1
                    ? L10n.text("\(selectedIDs.count) selected · Drag to copy", "已选 \(selectedIDs.count) 项 · 拖出以复制")
                    : L10n.text("\(items.count) items · Shift-click to select", "\(items.count) 项 · Shift 点击多选"))
            status.foregroundColor = muted.cgColor
        }
    }

    private func render(_ item: ShelfItem) {
        guard let rect = cardRect(for: item.id) else { return }
        let node = CALayer()
        node.name = "shelf.card.\(item.id.uuidString)"
        node.frame = rect
        node.allowsGroupOpacity = false
        collection.addSublayer(node)
        let selected = selectedIDs.contains(item.id)
        node.transform = selectionTransform(selected)
        let unavailable = item.availabilityError != nil
        let outline = cutCornerPath(CGRect(origin: .zero, size: rect.size), corner: 7)
        let back = CAShapeLayer()
        back.path = outline
        back.fillColor = NSColor(white: dark ? 0.76 : 0.89, alpha: 1).cgColor
        back.strokeColor = (selected ? yellow : NSColor(white: dark ? 0.88 : 0.39, alpha: 0.75)).cgColor
        back.lineWidth = selected ? 1.5 : 0.6
        node.addSublayer(back)
        HUDControlHighlightLayer.add(to: node, rect: node.bounds, shape: .cutCorner, framed: true)
        let image = CALayer()
        image.frame = CGRect(x: 9, y: 9, width: 30, height: 30)
        image.contents = cachedIcon(item).cgImage
        image.contentsGravity = .resizeAspect
        image.contentsScale = scale
        image.opacity = unavailable ? 0.45 : 1
        node.addSublayer(image)
        addText(item.name, rect: CGRect(x: 45, y: 9, width: 129, height: 17), size: 11.5, color: cardInk, parent: node, weight: .semibold)
        addText(unavailable ? L10n.text("Unavailable", "不可用") : typeLabel(item), rect: CGRect(x: 45, y: 28, width: 129, height: 14), size: 9, color: unavailable ? NSColor(srgbRed: 0.61, green: 0.19, blue: 0.11, alpha: 1) : cardInk.withAlphaComponent(0.70), parent: node)
        addText(sizeLabel(item), rect: CGRect(x: 10, y: 54, width: 97, height: 13), size: 9, color: cardInk.withAlphaComponent(0.72), parent: node)
        let line = CGMutablePath(); line.move(to: CGPoint(x: 8, y: 46)); line.addLine(to: CGPoint(x: 176, y: 46))
        stroke(line, color: cardInk.withAlphaComponent(0.17), parent: node, lineWidth: 0.6)
        for action in cardActions(item) {
            HUDControlHighlightLayer.add(to: node, rect: action.rect.offsetBy(dx: -rect.minX, dy: -rect.minY))
        }
        let activeColor = cardInk.withAlphaComponent(unavailable ? 0.22 : 0.82)
        drawEye(in: CGRect(x: 116, y: 54, width: 14, height: 10), color: activeColor, parent: node)
        drawFolder(in: CGRect(x: 139, y: 54, width: 14, height: 11), color: activeColor, parent: node)
        let cross = CGMutablePath(); cross.move(to: CGPoint(x: 164, y: 54)); cross.addLine(to: CGPoint(x: 173, y: 63)); cross.move(to: CGPoint(x: 173, y: 54)); cross.addLine(to: CGPoint(x: 164, y: 63))
        stroke(cross, color: cardInk.withAlphaComponent(0.80), parent: node, lineWidth: 1.1)
    }

    private func renderToolbar() {
        toolbar.sublayers?.forEach { $0.removeFromSuperlayer() }
        if confirmingClear {
            addText(L10n.text("Clear references only?", "仅清空文件引用？"), rect: CGRect(x: 12, y: 306, width: 202, height: 15), size: 10.5, color: primary, parent: toolbar)
        }
        for action in toolbarActions() {
            let highlighted = action.id == "shelf:add" || action.id == "shelf:confirmClear"
            let plate = CAShapeLayer()
            plate.frame = action.rect
            plate.path = cutCornerPath(CGRect(origin: .zero, size: action.rect.size), corner: 4)
            plate.fillColor = (highlighted ? yellow : NSColor(white: dark ? 0.82 : 0.9, alpha: 1)).cgColor
            plate.strokeColor = NSColor(white: dark ? 0.93 : 0.38, alpha: 0.65).cgColor
            plate.lineWidth = 0.6
            toolbar.addSublayer(plate)
            HUDControlHighlightLayer.add(to: plate, rect: plate.bounds, shape: .cutCorner, framed: true)
            let hasDepotIcon = action.id == "shelf:add" && EndfieldGameIcon.depot.add(to: plate,
                rect: CGRect(x: 7, y: 5, width: 17, height: 17), tint: cardInk, contentsScale: scale)
            let title: String
            switch action.id {
            case "shelf:add": title = hasDepotIcon ? action.label : "+ " + action.label
            case "shelf:previous": title = "‹"
            case "shelf:next": title = "›"
            default: title = action.label
            }
            let titleInset: CGFloat = hasDepotIcon ? 28 : 4
            addText(title, rect: CGRect(x: action.rect.minX + titleInset, y: action.rect.minY + 6,
                                       width: action.rect.width - titleInset - 4, height: 17),
                    size: 11, color: cardInk, parent: toolbar, weight: .semibold, alignment: .center)
        }
        if !confirmingClear {
            addText("\(pageIndex + 1) / \(pageCount)", rect: CGRect(x: 299, y: 306, width: 54, height: 15), size: 10, color: muted, parent: toolbar, alignment: .center)
        }
    }

    private func cutCornerPath(_ rect: CGRect, corner: CGFloat) -> CGPath {
        let p = CGMutablePath()
        p.move(to: CGPoint(x: rect.minX + corner, y: rect.minY))
        p.addLine(to: CGPoint(x: rect.maxX, y: rect.minY)); p.addLine(to: CGPoint(x: rect.maxX, y: rect.maxY - corner))
        p.addLine(to: CGPoint(x: rect.maxX - corner, y: rect.maxY)); p.addLine(to: CGPoint(x: rect.minX, y: rect.maxY))
        p.addLine(to: CGPoint(x: rect.minX, y: rect.minY + corner)); p.closeSubpath()
        return p
    }

    private func addText(_ string: String, rect: CGRect, size: CGFloat, color: NSColor, parent: CALayer,
                         weight: NSFont.Weight = .regular, alignment: CATextLayerAlignmentMode = .left, wrapped: Bool = false) {
        let text = CATextLayer()
        text.frame = rect; text.string = string
        text.font = NSFont.systemFont(ofSize: size, weight: weight); text.fontSize = size
        text.foregroundColor = color.cgColor; text.alignmentMode = alignment
        text.truncationMode = wrapped ? .none : .end; text.isWrapped = wrapped
        text.contentsScale = HUDRenderScale.contentScale(for: text, baseScale: scale)
        parent.addSublayer(text)
    }

    private func drawEye(in rect: CGRect, color: NSColor, parent: CALayer) {
        let p = CGMutablePath()
        p.move(to: CGPoint(x: rect.minX, y: rect.midY))
        p.addQuadCurve(to: CGPoint(x: rect.maxX, y: rect.midY), control: CGPoint(x: rect.midX, y: rect.minY - 2))
        p.addQuadCurve(to: CGPoint(x: rect.minX, y: rect.midY), control: CGPoint(x: rect.midX, y: rect.maxY + 2))
        p.addEllipse(in: CGRect(x: rect.midX - 1.5, y: rect.midY - 1.5, width: 3, height: 3))
        stroke(p, color: color, parent: parent, lineWidth: 1)
    }

    private func drawFolder(in rect: CGRect, color: NSColor, parent: CALayer, lineWidth: CGFloat = 1) {
        let p = CGMutablePath()
        p.move(to: CGPoint(x: rect.minX, y: rect.maxY)); p.addLine(to: CGPoint(x: rect.minX, y: rect.minY))
        p.addLine(to: CGPoint(x: rect.minX + rect.width * 0.4, y: rect.minY))
        p.addLine(to: CGPoint(x: rect.minX + rect.width * 0.54, y: rect.minY + rect.height * 0.25))
        p.addLine(to: CGPoint(x: rect.maxX, y: rect.minY + rect.height * 0.25)); p.addLine(to: CGPoint(x: rect.maxX, y: rect.maxY)); p.closeSubpath()
        stroke(p, color: color, parent: parent, lineWidth: lineWidth)
    }

    private func stroke(_ path: CGPath, color: NSColor, parent: CALayer, lineWidth: CGFloat) {
        let shape = CAShapeLayer(); shape.path = path; shape.fillColor = nil
        shape.strokeColor = color.cgColor; shape.lineWidth = lineWidth; shape.lineJoin = .round; shape.lineCap = .round
        parent.addSublayer(shape)
    }

    private func selectionTransform(_ selected: Bool) -> CATransform3D {
        CATransform3DMakeTranslation(0, selected ? -1.5 : 0, selected ? 5 : 0)
    }

    private func animateSelection(previous: Set<UUID>) {
        guard active, !reduceMotion() else { return }
        for item in visibleItems where previous.contains(item.id) != selectedIDs.contains(item.id) {
            guard let node = collection.sublayers?.first(where: { $0.name == "shelf.card.\(item.id.uuidString)" }) else { continue }
            let depth = CABasicAnimation(keyPath: "transform")
            depth.fromValue = NSValue(caTransform3D: selectionTransform(previous.contains(item.id)))
            depth.toValue = NSValue(caTransform3D: node.transform)
            depth.duration = 0.18
            depth.timingFunction = CAMediaTimingFunction(controlPoints: 0.16, 0.78, 0.25, 1)
            node.add(depth, forKey: "shelf.selection.depth")
        }
    }

    private func animateToolbar() {
        guard active, !reduceMotion() else { return }
        let slide = CABasicAnimation(keyPath: "sublayerTransform")
        slide.fromValue = NSValue(caTransform3D: CATransform3DMakeTranslation(0, 6, -6))
        slide.toValue = NSValue(caTransform3D: CATransform3DIdentity)
        slide.duration = 0.18
        slide.timingFunction = CAMediaTimingFunction(name: .easeOut)
        toolbar.add(slide, forKey: "shelf.toolbar.reveal")
    }

    private func settleAnimations(in node: CALayer) {
        node.removeAllAnimations()
        if let mask = node.mask { settleAnimations(in: mask) }
        node.sublayers?.forEach { settleAnimations(in: $0) }
    }

    private func reportUnavailable() { showError(L10n.text("File shelf storage is unavailable.", "文件暂存架存储不可用。")) }
    private func withoutActions(_ action: () -> Void) {
        CATransaction.begin(); CATransaction.setDisableActions(true); action(); CATransaction.commit()
    }
}
