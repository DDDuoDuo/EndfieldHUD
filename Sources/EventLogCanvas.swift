import AppKit
import QuartzCore

struct EventLogCanvasAction {
    let id: String
    let label: String
    let rect: CGRect
}

/// Retained center content. Only visible rows are drawn, with no polling or idle
/// animation; event observation is suspended while the module is hidden.
final class EventLogCanvas: NSObject, HUDModuleContentFactory {
    let layer = CALayer()
    var onChange: (() -> Void)?
    private(set) var selectedCategory: SystemEventCategory?
    private(set) var selectedID: UUID?
    private(set) var scrollOffset: CGFloat = 0
    var itemCount: Int { events.count }
    var filteredCount: Int { filtered.count }
    var accessibilityStatus: String {
        store.statusMessage ?? "\(filtered.count) events"
    }
    var accessibleActions: [EventLogCanvasAction] {
        controls() + visibleIndices.compactMap { index in
            let event = filtered[index]
            guard let rect = rowRect(for: event.id) else { return nil }
            return EventLogCanvasAction(id: "eventLog:row:\(event.id.uuidString)",
                label: [timestamp(event.createdAt), event.category.title, event.title, event.detail].filter { !$0.isEmpty }.joined(separator: ". "), rect: rect)
        }
    }

    static let viewport = CGRect(x: 12, y: 94, width: 376, height: 198)
    private static let rowHeight: CGFloat = 55
    private let store: SystemEventLog
    private var observer: UUID?
    private var events: [SystemEvent] = []
    private var filtered: [SystemEvent] = []
    private var active = false
    private var confirmingClear = false
    private var dark = true
    private var scale: CGFloat = 2
    private let header = CALayer()
    // Two bounded row planes let a new category replace the old one at a
    // single mask boundary. Only the incoming plane observes live changes.
    private let rowPages = [CALayer(), CALayer()]
    private var rowPageIndex = 0
    private var rows: CALayer { rowPages[rowPageIndex] }
    private let toolbar = CALayer()
    private let reduceMotion: () -> Bool
    private var filterTransition: HUDSubsectionHandoff!
    private var filterCleanup: DispatchWorkItem?
    private var filterGeneration = 0
    private let dateFormatter = DateFormatter()
    private var yellow: NSColor { HUDRuntimeAppearance.accent }
    private var primary: NSColor { NSColor(white: dark ? 0.94 : 0.11, alpha: 1) }
    private var muted: NSColor { NSColor(white: dark ? 0.68 : 0.38, alpha: 1) }
    private var ink: NSColor { NSColor(white: 0.13, alpha: 1) }
    private var maximumOffset: CGFloat { max(0, CGFloat(filtered.count) * Self.rowHeight - Self.viewport.height) }
    private var visibleIndices: Range<Int> {
        let first = min(filtered.count, max(0, Int(floor(scrollOffset / Self.rowHeight))))
        let end = min(filtered.count, Int(ceil((scrollOffset + Self.viewport.height) / Self.rowHeight)))
        return first..<max(first, end)
    }

    init(store: SystemEventLog, reduceMotion: @escaping () -> Bool = { HUDRuntimeAppearance.reduceMotion }) {
        self.store = store; self.reduceMotion = reduceMotion
        super.init()
        withoutActions {
            layer.name = "module.eventLog.canvas"
            layer.frame = CGRect(x: 0, y: 0, width: 400, height: 334)
            layer.allowsGroupOpacity = false
            header.frame = layer.bounds; layer.addSublayer(header)
            for (index, page) in rowPages.enumerated() {
                page.name = "eventLog.rows.\(index)"
                page.frame = Self.viewport; page.masksToBounds = true; page.allowsGroupOpacity = false
                layer.addSublayer(page)
            }
            toolbar.frame = layer.bounds; layer.addSublayer(toolbar)
        }
        filterTransition = HUDSubsectionHandoff(first: rowPages[0], second: rowPages[1], viewport: rows.bounds)
    }
    deinit {
        filterCleanup?.cancel()
        if let observer { store.removeObserver(observer) }
    }

    func makeContent(for module: HUDModule, style: HUDModuleContentStyle) -> CALayer {
        dark = style.dark; scale = style.contentsScale
        refresh(notify: false)
        return layer
    }
    func activate() {
        guard !active else { refresh(); return }
        active = true
        observer = store.observe { [weak self] in self?.refresh() }
        refresh()
    }
    func deactivate() {
        active = false
        settleFilterTransition()
        removeActionFeedback(in: layer)
        if let observer { store.removeObserver(observer) }; observer = nil
        confirmingClear = false
    }
    func updateRenderScale(_ value: CGFloat) {
        if reduceMotion() { settleFilterTransition(); removeActionFeedback(in: layer) }
        let next = value.isFinite ? min(8, max(1, value)) : 2
        guard scale != next else { return }
        scale = next
        if active { withoutActions { repaint() } }
    }

    @discardableResult func mouseDown(at point: CGPoint) -> Bool {
        guard point.x.isFinite, point.y.isFinite, layer.bounds.contains(point) else { return false }
        if let action = accessibleActions.first(where: { $0.rect.contains(point) }) { perform(actionID: action.id) }
        return true
    }
    @discardableResult func scroll(at point: CGPoint, delta: CGFloat) -> Bool {
        guard Self.viewport.contains(point), delta.isFinite else { return false }
        setScrollOffset(scrollOffset + delta)
        return true
    }
    func rowRect(for id: UUID) -> CGRect? {
        guard let index = filtered.firstIndex(where: { $0.id == id }) else { return nil }
        let rect = CGRect(x: Self.viewport.minX, y: Self.viewport.minY + CGFloat(index) * Self.rowHeight - scrollOffset,
                          width: Self.viewport.width - 7, height: Self.rowHeight - 4).intersection(Self.viewport)
        return rect.isNull || rect.height < 2 ? nil : rect
    }
    func selectNext(_ direction: Int) {
        guard !filtered.isEmpty else { return }
        let current = selectedID.flatMap { id in filtered.firstIndex { $0.id == id } }
        let index = min(filtered.count - 1, max(0, current.map { $0 + direction } ?? (direction < 0 ? filtered.count - 1 : 0)))
        selectedID = filtered[index].id
        let top = CGFloat(index) * Self.rowHeight
        if top < scrollOffset { scrollOffset = top }
        else if top + Self.rowHeight > scrollOffset + Self.viewport.height { scrollOffset = top + Self.rowHeight - Self.viewport.height }
        scrollOffset = min(maximumOffset, max(0, scrollOffset))
        confirmingClear = false
        withoutActions { repaint() }; animateSelection(); onChange?()
    }
    func scrollBy(_ delta: CGFloat) { if delta.isFinite { setScrollOffset(scrollOffset + delta) } }
    func cancelConfirmation() -> Bool {
        guard confirmingClear else { return false }
        confirmingClear = false; withoutActions { repaint() }; animateConfirmation(direction: -1); onChange?(); return true
    }
    func perform(actionID: String) {
        if actionID.hasPrefix("eventLog:category:") {
            let value = String(actionID.dropFirst("eventLog:category:".count))
            guard value == "all" || SystemEventCategory(rawValue: value) != nil else { return }
            let next = SystemEventCategory(rawValue: value)
            guard next != selectedCategory else { return }
            let categories: [SystemEventCategory?] = [nil] + SystemEventCategory.allCases.map(Optional.some)
            let direction: CGFloat = (categories.firstIndex(of: next) ?? 0) > (categories.firstIndex(of: selectedCategory) ?? 0) ? 1 : -1
            exchangeRows(direction: direction) {
                selectedCategory = next
                selectedID = nil; scrollOffset = 0; confirmingClear = false; refresh(notify: false)
            }
            return
        }
        if actionID.hasPrefix("eventLog:row:"), let id = UUID(uuidString: String(actionID.dropFirst("eventLog:row:".count))),
           filtered.contains(where: { $0.id == id }), rowRect(for: id) != nil {
            selectedID = id; confirmingClear = false; withoutActions { repaint() }; animateSelection(); onChange?(); return
        }
        switch actionID {
        case "eventLog:clear":
            guard !events.isEmpty else { return }
            confirmingClear = true; withoutActions { repaint() }; animateConfirmation(direction: 1); onChange?()
        case "eventLog:cancelClear": _ = cancelConfirmation()
        case "eventLog:confirmClear":
            guard confirmingClear else { return }
            exchangeRows(direction: -1) {
                confirmingClear = false; selectedID = nil; scrollOffset = 0; store.clear()
                if !active { refresh(notify: false) }
            }
            animateConfirmation(direction: -1)
        default: break
        }
    }

    private func exchangeRows(direction: CGFloat, change: () -> Void) {
        // Settle before reusing the older plane during rapid category changes.
        // The current rows remain frozen until the new mask replaces them;
        // repaint/scroll/store callbacks subsequently touch only `rows`.
        settleFilterTransition()
        removeActionFeedback(in: rows)
        rowPageIndex = 1 - rowPageIndex
        change()
        filterTransition.select(rowPageIndex, direction: direction, animated: active && !reduceMotion())
        if filterTransition.isTransitioning {
            let token = filterGeneration
            let cleanup = DispatchWorkItem { [weak self] in
                guard let self, token == self.filterGeneration else { return }
                self.settleFilterTransition()
            }
            filterCleanup = cleanup
            DispatchQueue.main.asyncAfter(deadline: .now() + HUDSubsectionTransition.duration, execute: cleanup)
        } else { settleFilterTransition() }
        onChange?()
    }
    private func settleFilterTransition() {
        filterGeneration += 1; filterCleanup?.cancel(); filterCleanup = nil
        filterTransition.settle()
        // In particular, cleared log text is released once it finishes exiting.
        // Each plane contains only the handful of rows within the viewport.
        withoutActions { rowPages[1 - rowPageIndex].sublayers?.forEach { $0.removeFromSuperlayer() } }
    }

    private func animateSelection() {
        guard active, !reduceMotion(), let selectedID,
              let row = rows.sublayers?.first(where: { $0.name == "eventLog.row." + selectedID.uuidString }),
              let outline = row.sublayers?.first as? CAShapeLayer else { return }
        let registration = CABasicAnimation(keyPath: "strokeEnd")
        registration.fromValue = 0; registration.toValue = 1
        registration.duration = 0.18; registration.timingFunction = CAMediaTimingFunction(name: .easeOut)
        outline.add(registration, forKey: "action.eventLog.selection")
    }
    private func animateConfirmation(direction: CGFloat) {
        guard active, !reduceMotion() else { return }
        // The toolbar also owns the category chips. Only its bottom clear
        // controls engage, keeping the header and active filter stationary.
        for control in toolbar.sublayers ?? [] where control.frame.minY >= 296 {
            let slide = CABasicAnimation(keyPath: "transform")
            slide.fromValue = NSValue(caTransform3D: CATransform3DMakeTranslation(10 * direction, 0, -8))
            slide.toValue = NSValue(caTransform3D: CATransform3DIdentity)
            slide.duration = 0.18; slide.timingFunction = CAMediaTimingFunction(name: .easeOut)
            control.add(slide, forKey: "action.eventLog.confirmation")
        }
    }
    private func removeActionFeedback(in node: CALayer) {
        for key in node.animationKeys() ?? [] where key.hasPrefix("action.eventLog.") { node.removeAnimation(forKey: key) }
        node.sublayers?.forEach { removeActionFeedback(in: $0) }
    }

    private func refresh(notify: Bool = true) {
        // Preserve the top visible row during new arrivals, unless already at top.
        let anchorIndex = Int(floor(scrollOffset / Self.rowHeight))
        let anchor = scrollOffset > 0 && filtered.indices.contains(anchorIndex) ? filtered[anchorIndex].id : nil
        let remainder = scrollOffset.truncatingRemainder(dividingBy: Self.rowHeight)
        events = store.events
        filtered = events.filter { selectedCategory == nil || $0.category == selectedCategory }
        if let anchor, let index = filtered.firstIndex(where: { $0.id == anchor }) { scrollOffset = CGFloat(index) * Self.rowHeight + remainder }
        scrollOffset = min(maximumOffset, max(0, scrollOffset))
        if let selectedID, !filtered.contains(where: { $0.id == selectedID }) { self.selectedID = nil }
        if events.isEmpty { confirmingClear = false }
        withoutActions { repaint() }
        if notify { onChange?() }
    }
    private func setScrollOffset(_ value: CGFloat) {
        let next = min(maximumOffset, max(0, value))
        guard next != scrollOffset else { return }
        scrollOffset = next
        withoutActions { renderRows() }; onChange?()
    }
    private func controls() -> [EventLogCanvasAction] {
        let categories: [SystemEventCategory?] = [nil] + SystemEventCategory.allCases.map(Optional.some)
        var actions = categories.enumerated().map { index, category in
            EventLogCanvasAction(id: "eventLog:category:" + (category?.rawValue ?? "all"),
                label: category?.title ?? "All",
                rect: CGRect(x: 12 + CGFloat(index % 4) * 95, y: 42 + CGFloat(index / 4) * 24, width: 91, height: 20))
        }
        if confirmingClear {
            actions += [EventLogCanvasAction(id: "eventLog:cancelClear", label: "Cancel", rect: CGRect(x: 222, y: 302, width: 72, height: 26)),
                        EventLogCanvasAction(id: "eventLog:confirmClear", label: "Clear", rect: CGRect(x: 301, y: 302, width: 87, height: 26))]
        } else if !events.isEmpty {
            actions.append(EventLogCanvasAction(id: "eventLog:clear", label: L10n.text("Clear log", "清空日志"), rect: CGRect(x: 275, y: 302, width: 113, height: 26)))
        }
        return actions
    }
    private func timestamp(_ date: Date) -> String {
        dateFormatter.locale = Locale(identifier: "en_US_POSIX")
        dateFormatter.dateFormat = "MM-dd HH:mm:ss"
        return dateFormatter.string(from: date)
    }

    private func repaint() {
        header.sublayers?.forEach { $0.removeFromSuperlayer() }
        toolbar.sublayers?.forEach { $0.removeFromSuperlayer() }
        text(HUDModule.eventLog.title, CGRect(x: 12, y: 0, width: 376, height: 20), size: 15, color: primary, parent: header, weight: .semibold)
        text(accessibilityStatus, CGRect(x: 12, y: 22, width: 376, height: 14), size: 9.5, color: muted, parent: header)
        for action in controls() {
            let selected = action.id == "eventLog:category:" + (selectedCategory?.rawValue ?? "all") || action.id == "eventLog:confirmClear"
            let shape = CAShapeLayer(); shape.frame = action.rect; shape.path = cutCorner(CGRect(origin: .zero, size: action.rect.size))
            shape.fillColor = (selected ? yellow : NSColor(white: dark ? 0.78 : 0.90, alpha: 1)).cgColor
            shape.strokeColor = NSColor(white: dark ? 0.94 : 0.35, alpha: 0.6).cgColor; shape.lineWidth = 0.6
            toolbar.addSublayer(shape)
            HUDControlHighlightLayer.add(to: shape, rect: shape.bounds, shape: .cutCorner, framed: true)
            text(action.label, action.rect.insetBy(dx: 4, dy: 3), size: 10, color: ink, parent: toolbar, weight: .medium, alignment: .center)
        }
        text(confirmingClear ? "Clear all saved events?" : "Local history",
             CGRect(x: 12, y: 309, width: confirmingClear ? 204 : 250, height: 14), size: 10, color: muted, parent: toolbar)
        renderRows()
    }
    private func renderRows() {
        rows.sublayers?.forEach { $0.removeFromSuperlayer() }
        if filtered.isEmpty {
            text(events.isEmpty ? "No events yet" : "No events in this category",
                 CGRect(x: 8, y: 81, width: 360, height: 26), size: 14, color: primary, parent: rows, alignment: .center)
            return
        }
        for index in visibleIndices {
            let event = filtered[index]
            let row = CALayer(); row.name = "eventLog.row." + event.id.uuidString
            row.frame = CGRect(x: 0, y: CGFloat(index) * Self.rowHeight - scrollOffset, width: Self.viewport.width - 7, height: Self.rowHeight - 4)
            rows.addSublayer(row)
            let plate = CAShapeLayer(); plate.path = cutCorner(row.bounds)
            plate.fillColor = NSColor(white: dark ? 0.16 : 0.91, alpha: 1).cgColor
            plate.strokeColor = (selectedID == event.id ? yellow : muted.withAlphaComponent(0.33)).cgColor
            plate.lineWidth = selectedID == event.id ? 1 : 0.5; row.addSublayer(plate)
            HUDControlHighlightLayer.add(to: row, rect: row.bounds, shape: .cutCorner)
            text(timestamp(event.createdAt), CGRect(x: 9, y: 5, width: 149, height: 12), size: 8.7, color: muted, parent: row)
            text(event.category.title, CGRect(x: 211, y: 5, width: 146, height: 12), size: 8.7, color: muted, parent: row, alignment: .right)
            text(event.title, CGRect(x: 9, y: 19, width: 348, height: 15), size: 11.5, color: primary, parent: row, weight: .medium)
            if !event.detail.isEmpty { text(event.detail, CGRect(x: 9, y: 35, width: 348, height: 13), size: 9.5, color: muted, parent: row) }
        }
        if maximumOffset > 0 {
            let height = max(18, Self.viewport.height * Self.viewport.height / (CGFloat(filtered.count) * Self.rowHeight))
            let thumb = CALayer(); thumb.name = "eventLog.scrollbar"
            thumb.frame = CGRect(x: Self.viewport.width - 3, y: (Self.viewport.height - height) * scrollOffset / maximumOffset, width: 2, height: height)
            thumb.backgroundColor = muted.withAlphaComponent(0.6).cgColor; rows.addSublayer(thumb)
        }
    }
    private func cutCorner(_ rect: CGRect) -> CGPath {
        let p = CGMutablePath(); p.move(to: CGPoint(x: rect.minX + 4, y: rect.minY)); p.addLine(to: CGPoint(x: rect.maxX, y: rect.minY))
        p.addLine(to: CGPoint(x: rect.maxX, y: rect.maxY - 4)); p.addLine(to: CGPoint(x: rect.maxX - 4, y: rect.maxY))
        p.addLine(to: CGPoint(x: rect.minX, y: rect.maxY)); p.addLine(to: CGPoint(x: rect.minX, y: rect.minY + 4)); p.closeSubpath(); return p
    }
    private func text(_ value: String, _ rect: CGRect, size: CGFloat, color: NSColor, parent: CALayer,
                      weight: NSFont.Weight = .regular, alignment: CATextLayerAlignmentMode = .left) {
        let text = CATextLayer(); text.frame = rect; text.string = value; text.font = NSFont.systemFont(ofSize: size, weight: weight); text.fontSize = size
        text.foregroundColor = color.cgColor; text.alignmentMode = alignment; text.truncationMode = .end
        text.contentsScale = HUDRenderScale.contentScale(for: text, baseScale: scale); parent.addSublayer(text)
    }
    private func withoutActions(_ body: () -> Void) { CATransaction.begin(); CATransaction.setDisableActions(true); body(); CATransaction.commit() }
}
