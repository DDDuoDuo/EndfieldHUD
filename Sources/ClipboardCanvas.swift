import AppKit
import QuartzCore

struct ClipboardCanvasAction {
    let id: String
    let label: String
    let rect: CGRect
}

/// A small retained history view. Capture and restoration belong to the shared
/// clipboard service; this scene only observes metadata and cached thumbnails.
final class ClipboardCanvas: NSObject, HUDModuleContentFactory {
    /// Keep presentation metadata only. A hidden HUD must not extend the life
    /// of full image/text/file payloads that the bounded store has evicted.
    private struct Row {
        let id: UUID
        let isPinned: Bool
        let kind: ClipboardKind
        let storedPreview: String
        let thumbnail: CGImage?
        init(_ item: ClipboardItem) {
            id = item.id; isPinned = item.isPinned; kind = item.kind
            storedPreview = item.preview; thumbnail = item.thumbnail
        }
        var preview: String {
            if kind == .image {
                for prefix in ["Image · ", "图像 · ", "图片 · "] where storedPreview.hasPrefix(prefix) {
                    return L10n.text("Image · ", "图片 · ") + storedPreview.dropFirst(prefix.count)
                }
            }
            return storedPreview
        }
    }
    let layer = CALayer()
    var onCopy: ((UUID) -> Bool)?
    var onChange: (() -> Void)?
    private(set) var selectedID: UUID?
    private(set) var scrollOffset: CGFloat = 0
    var itemCount: Int { items.count }
    var accessibilityStatus: String { feedback ?? store.statusMessage ?? defaultStatus }
    var accessibleActions: [ClipboardCanvasAction] {
        var result = toolbarActions()
        for item in visibleItems {
            guard let rect = rowRect(for: item.id) else { continue }
            result.append(ClipboardCanvasAction(id: action(item.id, "copy"),
                label: L10n.text("Copy: ", "复制：") + item.preview, rect: rect))
            result.append(contentsOf: rowActions(item))
        }
        return result
    }

    private static let rowHeight: CGFloat = 41
    private static let contentRect = CGRect(x: 12, y: 41, width: 376, height: 246)
    private let store: ClipboardStore
    private var observer: UUID?
    private var items: [Row] = []
    private var active = false
    private var dirty = true
    private var confirmingClear = false
    private var feedback: String?
    private var dark = true
    private var scale: CGFloat = 2
    private var yellow: NSColor { HUDRuntimeAppearance.accent }
    private let heading = CATextLayer()
    private let status = CATextLayer()
    private let rows = CALayer()
    private let scrollIndicator = CALayer()
    private var rowLayers: [UUID: CALayer] = [:]
    private let toolbar = CALayer()
    private let reduceMotion: () -> Bool
    private var collectionTransition: HUDSubsectionTransition!
    private var primary: NSColor { NSColor(white: dark ? 0.94 : 0.11, alpha: 1) }
    private var muted: NSColor { NSColor(white: dark ? 0.68 : 0.38, alpha: 1) }
    private let ink = NSColor(white: 0.14, alpha: 1)
    private var maximumOffset: CGFloat { max(0, CGFloat(items.count) * Self.rowHeight - Self.contentRect.height) }
    private var visibleIndices: Range<Int> {
        let first = min(items.count, max(0, Int(floor(scrollOffset / Self.rowHeight))))
        let end = min(items.count, Int(ceil((scrollOffset + Self.contentRect.height) / Self.rowHeight)))
        return first..<max(first, end)
    }
    private var visibleItems: ArraySlice<Row> {
        items[visibleIndices]
    }
    private var defaultStatus: String {
        L10n.text("\(items.count) / \(store.capacity) items · Click to copy", "\(items.count) / \(store.capacity) 项 · 点击复制")
    }

    init(store: ClipboardStore, reduceMotion: @escaping () -> Bool = { HUDRuntimeAppearance.reduceMotion }) {
        self.store = store; self.reduceMotion = reduceMotion
        super.init()
        withoutActions {
            layer.name = "module.clipboard.canvas"
            layer.frame = CGRect(x: 0, y: 0, width: 400, height: 334)
            layer.allowsGroupOpacity = false
            rows.name = "clipboard.rows"
            rows.frame = Self.contentRect
            rows.masksToBounds = true
            rows.allowsGroupOpacity = false
            layer.addSublayer(rows)
            scrollIndicator.name = "clipboard.scrollIndicator"
            scrollIndicator.cornerRadius = 1
            layer.addSublayer(scrollIndicator)
            heading.frame = CGRect(x: 12, y: 0, width: 376, height: 20)
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
        collectionTransition = HUDSubsectionTransition(content: rows, viewport: rows.bounds)
        observer = store.observe { [weak self] in
            guard let self else { return }
            self.dirty = true
            self.feedback = nil
            if self.active { self.refresh() }
        }
    }

    deinit { if let observer { store.removeObserver(observer) } }

    func makeContent(for module: HUDModule, style: HUDModuleContentStyle) -> CALayer {
        dark = style.dark
        scale = style.contentsScale
        refresh(notify: false)
        return layer
    }

    func activate() {
        active = true
        // makeContent prepares the incoming page before its reveal. Rebuild
        // only if a capture arrived during that transition.
        if dirty { refresh() }
    }

    func deactivate() {
        active = false
        collectionTransition.settle()
        removeActionFeedback(in: layer)
        confirmingClear = false
        feedback = nil
        dirty = true
    }

    func updateRenderScale(_ value: CGFloat) {
        if reduceMotion() { collectionTransition.settle(); removeActionFeedback(in: layer) }
        let next = value.isFinite ? min(8, max(1, value)) : 2
        guard scale != next else { return }
        scale = next
        if active { withoutActions { repaint() } }
    }

    @discardableResult func mouseDown(at point: CGPoint) -> Bool {
        guard point.x.isFinite, point.y.isFinite, layer.bounds.contains(point) else { return false }
        if let command = toolbarActions().first(where: { $0.rect.contains(point) }) {
            perform(actionID: command.id); return true
        }
        for item in visibleItems {
            guard let rect = rowRect(for: item.id), rect.contains(point) else { continue }
            if let command = rowActions(item).first(where: { $0.rect.contains(point) }) { perform(actionID: command.id) }
            else { copy(item.id) }
            return true
        }
        return true
    }

    @discardableResult func scroll(at point: CGPoint, delta: CGFloat) -> Bool {
        guard Self.contentRect.contains(point), delta.isFinite else { return false }
        let next = min(maximumOffset, max(0, scrollOffset + delta))
        guard next != scrollOffset else { return true }
        collectionTransition.settle()
        removeActionFeedback(in: rows)
        scrollOffset = next
        let toolbarChanged = confirmingClear
        confirmingClear = false
        feedback = nil
        withoutActions {
            // Retain the visible row layers during wheel/momentum events. Only
            // rows entering/leaving the viewport allocate or release artwork.
            layoutRows()
            status.string = accessibilityStatus
            status.foregroundColor = muted.cgColor
            if toolbarChanged { renderToolbar() }
        }
        onChange?()
        return true
    }

    func rowRect(for id: UUID) -> CGRect? {
        guard let rect = fullRowRect(for: id) else { return nil }
        return clipped(rect)
    }

    private func fullRowRect(for id: UUID) -> CGRect? {
        guard let index = items.firstIndex(where: { $0.id == id }) else { return nil }
        return CGRect(x: Self.contentRect.minX, y: Self.contentRect.minY + 2 + CGFloat(index) * Self.rowHeight - scrollOffset,
                      width: Self.contentRect.width, height: 37)
    }

    private func clipped(_ rect: CGRect) -> CGRect? {
        let visible = rect.intersection(Self.contentRect)
        return visible.isNull || visible.height < 2 ? nil : visible
    }

    private func revealRow(at index: Int) {
        let top = CGFloat(index) * Self.rowHeight
        if top < scrollOffset { scrollOffset = top }
        else if top + Self.rowHeight > scrollOffset + Self.contentRect.height {
            scrollOffset = top + Self.rowHeight - Self.contentRect.height
        }
        scrollOffset = min(maximumOffset, max(0, scrollOffset))
    }

    func selectNext(_ direction: Int) {
        guard !items.isEmpty else { return }
        let current = selectedID.flatMap { id in items.firstIndex { $0.id == id } }
        let index = min(items.count - 1, max(0, current.map { $0 + direction } ?? (direction < 0 ? items.count - 1 : 0)))
        selectedID = items[index].id
        revealRow(at: index)
        feedback = nil
        confirmingClear = false
        withoutActions { repaint() }
        onChange?()
    }

    func copySelection() { if let id = selectedID { copy(id) } }
    func copyVisibleItem(at index: Int) {
        let visible = visibleItems.filter { rowRect(for: $0.id) != nil }
        guard index >= 0, index < 6, index < visible.count else { return }
        copy(visible[index].id)
    }
    func deleteSelection() {
        guard let id = selectedID else { return }
        removeItem(id)
    }

    func perform(actionID value: String) {
        switch value {
        case "clipboard:clear":
            guard items.contains(where: { !$0.isPinned }) else { return }
            confirmingClear = true
            withoutActions { renderToolbar() }
            animateToolbar(direction: 1)
            onChange?()
        case "clipboard:cancelClear":
            confirmingClear = false
            withoutActions { renderToolbar() }
            animateToolbar(direction: -1)
            onChange?()
        case "clipboard:confirmClear":
            guard confirmingClear else { return }
            confirmingClear = false
            let positions = visibleRowPositions()
            if store.clearUnpinned() { animateCollectionChange(from: positions) }
            animateToolbar(direction: -1)
        default:
            let parts = value.split(separator: ":")
            guard parts.count == 3, parts[0] == "clipboard", let id = UUID(uuidString: String(parts[1])),
                  items.contains(where: { $0.id == id }) else { return }
            switch parts[2] {
            case "copy": copy(id)
            case "pin": if store.togglePin(id: id) { animateRow(id) }
            case "remove": removeItem(id)
            default: break
            }
        }
    }

    private func copy(_ id: UUID) {
        guard items.contains(where: { $0.id == id }) else { return }
        selectedID = id
        confirmingClear = false
        let success = onCopy?(id) ?? false
        feedback = success ? L10n.text("Copied", "已复制") : (store.statusMessage ?? L10n.text("Could not restore this item", "无法恢复此项目"))
        withoutActions { repaint() }
        if success { animateRow(id) }
        onChange?()
    }

    private func removeItem(_ id: UUID) {
        let positions = visibleRowPositions()
        if store.remove(id: id) { animateCollectionChange(from: positions) }
    }
    private func visibleRowPositions() -> [String: CGPoint] {
        Dictionary(uniqueKeysWithValues: (rows.sublayers ?? []).compactMap { row in
            guard let name = row.name, name.hasPrefix("clipboard.row.") else { return nil }
            return (name, row.presentation()?.position ?? row.position)
        })
    }
    private func animateCollectionChange(from positions: [String: CGPoint]) {
        guard active, !reduceMotion() else { return }
        collectionTransition.settle()
        var moved = false
        for row in rows.sublayers ?? [] {
            guard let name = row.name, name.hasPrefix("clipboard.row.") else { continue }
            let previous = positions[name] ?? CGPoint(x: row.position.x, y: row.position.y + 30)
            guard previous != row.position else { continue }
            let reflow = CABasicAnimation(keyPath: "position")
            reflow.fromValue = NSValue(point: previous); reflow.toValue = NSValue(point: row.position)
            reflow.duration = 0.20; reflow.timingFunction = CAMediaTimingFunction(name: .easeOut)
            row.add(reflow, forKey: "action.clipboard.reflow"); moved = true
        }
        if !moved { collectionTransition.reveal(direction: -1, animated: true) }
    }
    private func animateRow(_ id: UUID) {
        guard active, !reduceMotion(), let row = rows.sublayers?.first(where: { $0.name == "clipboard.row.\(id.uuidString)" }) else { return }
        let engage = CABasicAnimation(keyPath: "transform")
        engage.fromValue = NSValue(caTransform3D: CATransform3DMakeTranslation(4, 0, -8))
        engage.toValue = NSValue(caTransform3D: CATransform3DIdentity)
        engage.duration = 0.18; engage.timingFunction = CAMediaTimingFunction(name: .easeOut)
        row.add(engage, forKey: "action.clipboard.engage")
        if let outline = row.sublayers?.first as? CAShapeLayer {
            let trace = CABasicAnimation(keyPath: "strokeEnd"); trace.fromValue = 0; trace.toValue = 1
            trace.duration = 0.18; trace.timingFunction = engage.timingFunction
            outline.add(trace, forKey: "action.clipboard.register")
        }
    }
    private func animateToolbar(direction: CGFloat) {
        guard active, !reduceMotion() else { return }
        let slide = CABasicAnimation(keyPath: "sublayerTransform")
        slide.fromValue = NSValue(caTransform3D: CATransform3DMakeTranslation(10 * direction, 0, -8))
        slide.toValue = NSValue(caTransform3D: CATransform3DIdentity)
        slide.duration = 0.18; slide.timingFunction = CAMediaTimingFunction(name: .easeOut)
        toolbar.add(slide, forKey: "action.clipboard.confirmation")
    }
    private func removeActionFeedback(in node: CALayer) {
        for key in node.animationKeys() ?? [] where key.hasPrefix("action.clipboard.") { node.removeAnimation(forKey: key) }
        node.sublayers?.forEach { removeActionFeedback(in: $0) }
    }

    private func refresh(notify: Bool = true) {
        if dirty {
            // Keep the item under the user's eyes stable when captures prepend
            // history. At the top, new captures should appear immediately.
            let first = Int(floor(scrollOffset / Self.rowHeight))
            let anchor = scrollOffset > 0 && items.indices.contains(first) ? items[first].id : nil
            let remainder = scrollOffset.truncatingRemainder(dividingBy: Self.rowHeight)
            items = store.items.map(Row.init)
            if let anchor, let index = items.firstIndex(where: { $0.id == anchor }) {
                scrollOffset = CGFloat(index) * Self.rowHeight + remainder
            }
            dirty = false
        }
        if let id = selectedID, !items.contains(where: { $0.id == id }) { selectedID = nil }
        scrollOffset = min(maximumOffset, max(0, scrollOffset))
        if !items.contains(where: { !$0.isPinned }) { confirmingClear = false }
        withoutActions { repaint() }
        if notify { onChange?() }
    }

    private func action(_ id: UUID, _ verb: String) -> String { "clipboard:\(id.uuidString):\(verb)" }
    private func rowActions(_ item: Row, clippedToViewport: Bool = true) -> [ClipboardCanvasAction] {
        guard let rect = fullRowRect(for: item.id) else { return [] }
        let actions = [ClipboardCanvasAction(id: action(item.id, "pin"), label: (item.isPinned ? L10n.text("Unpin: ", "取消固定：") : L10n.text("Pin: ", "固定：")) + item.preview,
                                      rect: CGRect(x: rect.minX + 320, y: rect.minY + 6, width: 23, height: 25)),
                ClipboardCanvasAction(id: action(item.id, "remove"), label: L10n.text("Delete: ", "删除：") + item.preview,
                                      rect: CGRect(x: rect.minX + 348, y: rect.minY + 6, width: 23, height: 25))]
        guard clippedToViewport else { return actions }
        return actions.compactMap { action in
            guard let rect = clipped(action.rect) else { return nil }
            return ClipboardCanvasAction(id: action.id, label: action.label, rect: rect)
        }
    }

    private func toolbarActions() -> [ClipboardCanvasAction] {
        if confirmingClear {
            return [ClipboardCanvasAction(id: "clipboard:cancelClear", label: L10n.text("Cancel", "取消"), rect: CGRect(x: 218, y: 299, width: 67, height: 27)),
                    ClipboardCanvasAction(id: "clipboard:confirmClear", label: L10n.text("Clear", "清空"), rect: CGRect(x: 294, y: 299, width: 94, height: 27))]
        }
        var result: [ClipboardCanvasAction] = []
        if items.contains(where: { !$0.isPinned }) { result.append(ClipboardCanvasAction(id: "clipboard:clear", label: L10n.text("Clear unpinned", "清空未固定项"), rect: CGRect(x: 12, y: 299, width: 140, height: 27))) }
        return result
    }

    private func repaint() {
        heading.string = HUDModule.clipboard.title
        heading.foregroundColor = primary.cgColor
        heading.contentsScale = HUDRenderScale.contentScale(for: heading, baseScale: scale)
        status.string = accessibilityStatus
        status.foregroundColor = feedback == L10n.text("Copied", "已复制") ? (dark ? yellow : yellow.blended(withFraction: 0.40, of: .black) ?? yellow).cgColor : muted.cgColor
        status.contentsScale = HUDRenderScale.contentScale(for: status, baseScale: scale)
        rows.sublayers?.forEach { $0.removeFromSuperlayer() }
        rowLayers.removeAll()
        if items.isEmpty {
            addText(L10n.text("Your clipboard history appears here", "剪贴板历史将在此显示"), rect: CGRect(x: 12, y: 104, width: 352, height: 46), size: 14, color: primary, parent: rows, alignment: .center, wrapped: true)
            addText(L10n.text("Copy text, links, images or files", "复制文字、链接、图片或文件"), rect: CGRect(x: 12, y: 156, width: 352, height: 32), size: 10.5, color: muted, parent: rows, alignment: .center, wrapped: true)
        }
        layoutRows()
        renderToolbar()
    }

    private func layoutRows() {
        let expected = Set(visibleItems.map(\.id))
        for id in Array(rowLayers.keys) where !expected.contains(id) {
            rowLayers.removeValue(forKey: id)?.removeFromSuperlayer()
        }
        for index in visibleIndices {
            let item = items[index]
            if let row = rowLayers[item.id], let rect = fullRowRect(for: item.id) {
                row.frame = rect.offsetBy(dx: -Self.contentRect.minX, dy: -Self.contentRect.minY)
            } else { render(item, number: index + 1) }
        }
        scrollIndicator.isHidden = maximumOffset == 0
        if maximumOffset > 0 {
            let height = max(24, Self.contentRect.height * Self.contentRect.height / (CGFloat(items.count) * Self.rowHeight))
            scrollIndicator.frame = CGRect(x: Self.contentRect.maxX + 4,
                y: Self.contentRect.minY + (Self.contentRect.height - height) * scrollOffset / maximumOffset,
                width: 2, height: height)
            scrollIndicator.backgroundColor = yellow.withAlphaComponent(0.55).cgColor
        }
    }

    private func render(_ item: Row, number: Int) {
        guard let rect = fullRowRect(for: item.id) else { return }
        let row = CALayer(); row.name = "clipboard.row.\(item.id.uuidString)"
        row.frame = rect.offsetBy(dx: -Self.contentRect.minX, dy: -Self.contentRect.minY)
        row.allowsGroupOpacity = false
        rows.addSublayer(row)
        rowLayers[item.id] = row
        let selected = item.id == selectedID
        let plate = CAShapeLayer()
        plate.path = cutCorner(CGRect(origin: .zero, size: rect.size), corner: 5)
        plate.fillColor = NSColor(white: dark ? 0.77 : 0.90, alpha: 1).cgColor
        plate.strokeColor = (selected ? yellow : NSColor(white: dark ? 0.90 : 0.37, alpha: 0.7)).cgColor
        plate.lineWidth = selected ? 1.4 : 0.6
        row.addSublayer(plate)
        HUDControlHighlightLayer.add(to: row, rect: row.bounds, shape: .cutCorner, framed: true)
        addText(String(format: "%02d", number), rect: CGRect(x: 7, y: 10, width: 23, height: 18), size: 10.5, color: ink.withAlphaComponent(0.65), parent: row, weight: .semibold, alignment: .center)
        if let thumbnail = item.thumbnail {
            let image = CALayer(); image.frame = CGRect(x: 37, y: 6, width: 25, height: 25)
            image.contents = thumbnail; image.contentsGravity = .resizeAspect; image.contentsScale = scale
            row.addSublayer(image)
        } else { drawKind(item.kind, parent: row) }
        addText(item.preview, rect: CGRect(x: 71, y: 5, width: 243, height: 17), size: 11.5, color: ink, parent: row, weight: .medium)
        let title: String
        switch item.kind {
        case .text: title = L10n.text("Text", "文字")
        case .url: title = L10n.text("Link", "链接")
        case .image: title = L10n.text("Image", "图片")
        case .files: title = L10n.text("Files", "文件")
        }
        addText(title + (item.isPinned ? L10n.text(" · Pinned", " · 已固定") : ""), rect: CGRect(x: 71, y: 23, width: 243, height: 11), size: 8.5, color: ink.withAlphaComponent(0.64), parent: row)
        for action in rowActions(item, clippedToViewport: false) {
            HUDControlHighlightLayer.add(to: row, rect: action.rect.offsetBy(dx: -rect.minX, dy: -rect.minY))
        }
        let pin = CGMutablePath(); pin.move(to: CGPoint(x: 327, y: 11)); pin.addLine(to: CGPoint(x: 336, y: 11)); pin.move(to: CGPoint(x: 329, y: 11)); pin.addLine(to: CGPoint(x: 329, y: 17)); pin.addLine(to: CGPoint(x: 326, y: 20)); pin.addLine(to: CGPoint(x: 337, y: 20)); pin.addLine(to: CGPoint(x: 334, y: 17)); pin.addLine(to: CGPoint(x: 334, y: 11)); pin.move(to: CGPoint(x: 331.5, y: 20)); pin.addLine(to: CGPoint(x: 331.5, y: 27))
        if item.isPinned {
            let halo = CAShapeLayer(); halo.path = CGPath(ellipseIn: CGRect(x: 321, y: 7, width: 22, height: 23), transform: nil); halo.fillColor = yellow.cgColor; row.addSublayer(halo)
        }
        stroke(pin, color: ink.withAlphaComponent(item.isPinned ? 1 : 0.68), parent: row)
        let x = CGMutablePath(); x.move(to: CGPoint(x: 356, y: 13)); x.addLine(to: CGPoint(x: 365, y: 23)); x.move(to: CGPoint(x: 365, y: 13)); x.addLine(to: CGPoint(x: 356, y: 23))
        stroke(x, color: ink.withAlphaComponent(0.76), parent: row)
    }

    private func drawKind(_ kind: ClipboardKind, parent: CALayer) {
        let gameIcon: EndfieldGameIcon? = kind == .text ? .operationalManual : kind == .files ? .depot : nil
        if gameIcon?.add(to: parent, rect: CGRect(x: 37, y: 6, width: 25, height: 25),
                         tint: ink.withAlphaComponent(0.78), contentsScale: scale) == true { return }
        let p = CGMutablePath()
        switch kind {
        case .text:
            p.move(to: CGPoint(x: 39, y: 10)); p.addLine(to: CGPoint(x: 59, y: 10)); p.move(to: CGPoint(x: 49, y: 10)); p.addLine(to: CGPoint(x: 49, y: 28)); p.move(to: CGPoint(x: 44, y: 28)); p.addLine(to: CGPoint(x: 54, y: 28))
        case .url:
            p.addRoundedRect(in: CGRect(x: 36, y: 10, width: 16, height: 12), cornerWidth: 5, cornerHeight: 5)
            p.addRoundedRect(in: CGRect(x: 46, y: 16, width: 16, height: 12), cornerWidth: 5, cornerHeight: 5)
        case .image:
            p.addRect(CGRect(x: 37, y: 8, width: 25, height: 21)); p.move(to: CGPoint(x: 39, y: 25)); p.addLine(to: CGPoint(x: 47, y: 17)); p.addLine(to: CGPoint(x: 53, y: 23)); p.addLine(to: CGPoint(x: 58, y: 19)); p.addLine(to: CGPoint(x: 61, y: 22))
            p.addEllipse(in: CGRect(x: 53, y: 11, width: 4, height: 4))
        case .files:
            p.move(to: CGPoint(x: 38, y: 29)); p.addLine(to: CGPoint(x: 38, y: 9)); p.addLine(to: CGPoint(x: 46, y: 9)); p.addLine(to: CGPoint(x: 50, y: 13)); p.addLine(to: CGPoint(x: 62, y: 13)); p.addLine(to: CGPoint(x: 62, y: 29)); p.closeSubpath()
        }
        stroke(p, color: ink.withAlphaComponent(0.78), parent: parent, width: 1.5)
    }

    private func renderToolbar() {
        toolbar.sublayers?.forEach { $0.removeFromSuperlayer() }
        if confirmingClear { addText(L10n.text("Pinned items will stay", "保留已固定项目"), rect: CGRect(x: 12, y: 306, width: 202, height: 15), size: 10.5, color: primary, parent: toolbar) }
        for command in toolbarActions() {
            let plate = CAShapeLayer(); plate.frame = command.rect; plate.path = cutCorner(CGRect(origin: .zero, size: command.rect.size), corner: 4)
            plate.fillColor = (command.id == "clipboard:confirmClear" ? yellow : NSColor(white: dark ? 0.82 : 0.9, alpha: 1)).cgColor
            plate.strokeColor = NSColor(white: dark ? 0.93 : 0.38, alpha: 0.65).cgColor; plate.lineWidth = 0.6; toolbar.addSublayer(plate)
            HUDControlHighlightLayer.add(to: plate, rect: plate.bounds, shape: .cutCorner, framed: true)
            addText(command.label, rect: CGRect(x: command.rect.minX + 4, y: command.rect.minY + 6, width: command.rect.width - 8, height: 17), size: 11, color: ink, parent: toolbar, weight: .semibold, alignment: .center)
        }
    }

    private func cutCorner(_ rect: CGRect, corner: CGFloat) -> CGPath {
        let p = CGMutablePath(); p.move(to: CGPoint(x: rect.minX + corner, y: rect.minY)); p.addLine(to: CGPoint(x: rect.maxX, y: rect.minY)); p.addLine(to: CGPoint(x: rect.maxX, y: rect.maxY - corner)); p.addLine(to: CGPoint(x: rect.maxX - corner, y: rect.maxY)); p.addLine(to: CGPoint(x: rect.minX, y: rect.maxY)); p.addLine(to: CGPoint(x: rect.minX, y: rect.minY + corner)); p.closeSubpath(); return p
    }
    private func addText(_ value: String, rect: CGRect, size: CGFloat, color: NSColor, parent: CALayer,
                         weight: NSFont.Weight = .regular, alignment: CATextLayerAlignmentMode = .left, wrapped: Bool = false) {
        let text = CATextLayer(); text.frame = rect; text.string = value; text.font = NSFont.systemFont(ofSize: size, weight: weight); text.fontSize = size
        text.foregroundColor = color.cgColor; text.alignmentMode = alignment; text.isWrapped = wrapped; text.truncationMode = wrapped ? .none : .end
        text.contentsScale = HUDRenderScale.contentScale(for: text, baseScale: scale); parent.addSublayer(text)
    }
    private func stroke(_ path: CGPath, color: NSColor, parent: CALayer, width: CGFloat = 1.1) {
        let shape = CAShapeLayer(); shape.path = path; shape.fillColor = nil; shape.strokeColor = color.cgColor; shape.lineWidth = width; shape.lineCap = .round; shape.lineJoin = .round; parent.addSublayer(shape)
    }
    private func withoutActions(_ body: () -> Void) { CATransaction.begin(); CATransaction.setDisableActions(true); body(); CATransaction.commit() }
}
