import AppKit
import QuartzCore

/// Stable data-series colors keep paired metrics recognizable across themes.
/// Navigation and controls continue to use the configured HUD accent.
enum ActivityGraphPalette {
    static let yellow = NSColor(srgbRed: 0.98, green: 0.83, blue: 0.12, alpha: 1)
    static let blue = NSColor(srgbRed: 0.31, green: 0.73, blue: 0.96, alpha: 1)
}

final class ActivityMonitorCanvas: NSObject, HUDModuleContentFactory {
    let layer = CALayer()
    var onChange: (() -> Void)?
    var accessibleActions: [TelemetryCanvasAction] {
        var result = [TelemetryCanvasAction(id: "activity:overview", label: L10n.text("Overview", "概览"), rect: CGRect(x: 12, y: 25, width: 82, height: 23)),
                      TelemetryCanvasAction(id: "activity:apps", label: L10n.text("Apps", "应用"), rect: CGRect(x: 100, y: 25, width: 82, height: 23))]
        if isShowingApps {
            for column in appColumns {
                let direction = column.key == sortKey
                    ? (sortDescending ? L10n.text(", descending", "，降序") : L10n.text(", ascending", "，升序")) : ""
                result.append(TelemetryCanvasAction(id: "activity:sort:" + column.key.rawValue,
                    label: L10n.text("Sort by ", "排序：") + column.title + direction,
                    rect: column.frame.insetBy(dx: 0, dy: -5)))
            }
        }
        return result
    }
    var animationCount: Int {
        TelemetryArtwork.animationCount(layer) + (pageTransition?.animationCount ?? 0)
            + (sortTransition?.animationCount ?? 0)
    }
    var accessibilityStatus: String {
        if isShowingApps {
            return apps.snapshot.items.map { item in
                [item.name, "CPU " + appCPU(item.cpuPercent), TelemetryArtwork.bytes(item.memoryBytes.map { Double($0) }),
                 "↑ " + TelemetryArtwork.rate(item.uploadBytesPerSecond), "↓ " + TelemetryArtwork.rate(item.downloadBytesPerSecond),
                 L10n.text("Read ", "读取 ") + TelemetryArtwork.rate(item.diskReadBytesPerSecond),
                 L10n.text("Write ", "写入 ") + TelemetryArtwork.rate(item.diskWriteBytesPerSecond)].joined(separator: ", ")
            }.joined(separator: "; ")
        }
        let value = controller.snapshot
        let values = ["CPU " + TelemetryArtwork.percent(value.cpuPercent),
            L10n.text("Memory ", "内存 ") + TelemetryArtwork.bytes(value.memoryUsedBytes.map { Double($0) }) + " / " + TelemetryArtwork.bytes(value.memoryTotalBytes.map { Double($0) }),
            L10n.text("Upload ", "上传 ") + TelemetryArtwork.rate(value.uploadBytesPerSecond),
            L10n.text("Download ", "下载 ") + TelemetryArtwork.rate(value.downloadBytesPerSecond),
            L10n.text("Disk read ", "磁盘读取 ") + TelemetryArtwork.rate(value.diskReadBytesPerSecond),
            L10n.text("Disk write ", "磁盘写入 ") + TelemetryArtwork.rate(value.diskWriteBytesPerSecond)]
        return values.joined(separator: "; ")
    }
    private final class Row {
        let layer: CALayer
        let tick = CALayer()
        let title: CATextLayer, subtitle: CATextLayer, primary: CATextLayer, secondary: CATextLayer, tertiary: CATextLayer
        let graph: TelemetryGraph
        init(index: Int, parent: CALayer, colors: [NSColor]) {
            layer = TelemetryArtwork.plate(CGRect(x: 12, y: 60 + CGFloat(index) * 60, width: 376, height: 54), parent: parent, name: "activity.row.\(index)")
            tick.frame = CGRect(x: 0, y: 12, width: 2, height: 30); tick.backgroundColor = colors[0].cgColor; layer.addSublayer(tick)
            title = TelemetryArtwork.text("activity.title.\(index)", frame: CGRect(x: 9, y: 10, width: 70, height: 16), size: 11, parent: layer, weight: .semibold)
            subtitle = TelemetryArtwork.text("activity.subtitle.\(index)", frame: CGRect(x: 9, y: 31, width: 70, height: 12), size: 8.5, parent: layer)
            graph = TelemetryGraph(name: "activity.graph.\(index)", frame: CGRect(x: 85, y: 9, width: 165, height: 36), colors: colors)
            layer.addSublayer(graph.layer)
            primary = TelemetryArtwork.text("activity.value.\(index)", frame: CGRect(x: 262, y: 6, width: 104, height: 19), size: index < 2 ? 14 : 11.5, parent: layer, weight: .medium, alignment: .right)
            secondary = TelemetryArtwork.text("activity.secondary.\(index)", frame: CGRect(x: 259, y: 27, width: 107, height: 14), size: 10, parent: layer, alignment: .right)
            tertiary = TelemetryArtwork.text("activity.tertiary.\(index)", frame: CGRect(x: 257, y: 41, width: 109, height: 11), size: 8, parent: layer, alignment: .right)
        }
    }
    private let controller: SystemActivityMonitor
    private let apps: AppActivityMonitor
    private var appObserver: UUID?
    private(set) var isShowingApps = false
    private(set) var scrollOffset: CGFloat = 0
    private var sortKey: AppActivitySortKey = .cpu
    private var sortDescending = true
    private let overviewLayer = CALayer()
    private let appLayer = CALayer()
    private var pageTransition: HUDSubsectionHandoff!
    private var sortTransition: HUDSubsectionTransition!
    private let appRows = CALayer()
    private let appEmpty = CATextLayer()
    private let appScrollBar = CALayer()
    private var appRowPool: [AppRow] = []
    private var controls: [(CALayer, CATextLayer)] = []
    private var appHeaders: [CATextLayer] = []
    private let appHeaderRule = CALayer()
    private var iconCache: [String: NSImage] = [:]
    static let appsViewport = CGRect(x: 12, y: 84, width: 376, height: 226)
    private struct AppColumn {
        let key: AppActivitySortKey
        let title: String
        let frame: CGRect
    }
    private var appColumns: [AppColumn] {
        let values: [(AppActivitySortKey, String, CGFloat, CGFloat)] = [
            (.name, L10n.text("App", "应用"), 12, 128), (.cpu, "CPU", 146, 45),
            (.memory, L10n.text("Memory", "内存"), 195, 52),
            (.network, L10n.text("Network", "网络"), 251, 65), (.disk, L10n.text("Disk", "磁盘"), 317, 65)
        ]
        return values.map { AppColumn(key: $0.0, title: $0.1, frame: CGRect(x: $0.2, y: 61, width: $0.3, height: 15)) }
    }
    private final class AppRow {
        let plate: CALayer
        let icon = CALayer()
        let name: CATextLayer, cpu: CATextLayer, memory: CATextLayer
        let upload: CATextLayer, download: CATextLayer, read: CATextLayer, write: CATextLayer
        init(_ index: Int, parent: CALayer) {
            plate = TelemetryArtwork.plate(.zero, parent: parent, name: "activity.app.\(index)")
            icon.frame = CGRect(x: 7, y: 13, width: 18, height: 18); icon.contentsGravity = .resizeAspect; plate.addSublayer(icon)
            name = TelemetryArtwork.text("activity.app.name.\(index)", frame: CGRect(x: 31, y: 12, width: 100, height: 28), size: 10, parent: plate, weight: .medium, wrapped: true)
            cpu = TelemetryArtwork.text("activity.app.cpu.\(index)", frame: CGRect(x: 134, y: 14, width: 45, height: 18), size: 10, parent: plate, alignment: .right)
            memory = TelemetryArtwork.text("activity.app.memory.\(index)", frame: CGRect(x: 183, y: 14, width: 52, height: 18), size: 10, parent: plate, alignment: .right)
            upload = TelemetryArtwork.text("activity.app.upload.\(index)", frame: CGRect(x: 239, y: 7, width: 65, height: 15), size: 8.5, parent: plate, alignment: .right)
            download = TelemetryArtwork.text("activity.app.download.\(index)", frame: CGRect(x: 239, y: 26, width: 65, height: 15), size: 8.5, parent: plate, alignment: .right)
            read = TelemetryArtwork.text("activity.app.read.\(index)", frame: CGRect(x: 305, y: 7, width: 65, height: 15), size: 8.5, parent: plate, alignment: .right)
            write = TelemetryArtwork.text("activity.app.write.\(index)", frame: CGRect(x: 305, y: 26, width: 65, height: 15), size: 8.5, parent: plate, alignment: .right)
        }
    }
    private let reduceMotion: () -> Bool
    private var observer: UUID?
    private var active = false
    private var hasVisibleSample = false
    private var lastTimestamp: Date?
    private var dark = true
    private var scale: CGFloat = 2
    private var rows: [Row] = []
    private let heading: CATextLayer, subtitle: CATextLayer, footer: CATextLayer

    init(controller: SystemActivityMonitor, apps: AppActivityMonitor = .fixture(), reduceMotion: @escaping () -> Bool = { HUDRuntimeAppearance.reduceMotion }) {
        self.controller = controller; self.apps = apps; self.reduceMotion = reduceMotion
        layer.name = "module.activityMonitor.canvas"; layer.frame = CGRect(x: 0, y: 0, width: 400, height: 334)
        heading = TelemetryArtwork.text("activity.heading", frame: CGRect(x: 12, y: 0, width: 376, height: 20), size: 15, parent: layer, weight: .semibold)
        subtitle = TelemetryArtwork.text("activity.caption", frame: CGRect(x: 12, y: 24, width: 376, height: 13), size: 9, parent: layer)
        footer = TelemetryArtwork.text("activity.footer", frame: CGRect(x: 12, y: 296, width: 376, height: 33), size: 9, parent: layer, wrapped: true)
        super.init()
        overviewLayer.name = "activity.overview"; overviewLayer.frame = layer.bounds; layer.addSublayer(overviewLayer)
        rows = [Row(index: 0, parent: overviewLayer, colors: [ActivityGraphPalette.yellow]), Row(index: 1, parent: overviewLayer, colors: [ActivityGraphPalette.blue]),
                Row(index: 2, parent: overviewLayer, colors: [ActivityGraphPalette.yellow, ActivityGraphPalette.blue]), Row(index: 3, parent: overviewLayer, colors: [ActivityGraphPalette.yellow, ActivityGraphPalette.blue])]
        buildAppRows()
        let viewport = CGRect(x: 8, y: 54, width: 384, height: 258)
        pageTransition = HUDSubsectionHandoff(first: overviewLayer, second: appLayer, viewport: viewport)
        sortTransition = HUDSubsectionTransition(content: appRows, viewport: appRows.bounds)
        render(animated: false)
    }
    deinit {
        if let observer { controller.removeObserver(observer) }
        if let appObserver { apps.removeObserver(appObserver) }; if active { controller.deactivate(); apps.deactivate() }
        TelemetryArtwork.removeAnimations(layer)
    }
    func makeContent(for module: HUDModule, style: HUDModuleContentStyle) -> CALayer {
        dark = style.dark; scale = style.contentsScale; render(animated: false); return layer
    }
    func activate() {
        guard !active else { return }
        active = true; hasVisibleSample = false
        observer = controller.observe { [weak self] value in
            guard let self, self.active else { return }
            let animate = self.hasVisibleSample && self.lastTimestamp != value.timestamp && !self.reduceMotion()
            self.lastTimestamp = value.timestamp; self.hasVisibleSample = true
            if !self.isShowingApps { self.render(animated: animate); self.onChange?() }
        }
        controller.activate()
        appObserver = apps.observe { [weak self] _ in
            guard let self, self.active, self.isShowingApps else { return }
            self.renderApps(); self.onChange?()
        }
        if isShowingApps { apps.activate() }
        render(animated: false)
    }
    func deactivate() {
        active = false; hasVisibleSample = false
        if let observer { controller.removeObserver(observer) }; observer = nil
        if let appObserver { apps.removeObserver(appObserver) }; appObserver = nil
        apps.deactivate(); controller.deactivate(); TelemetryArtwork.removeAnimations(layer)
        pageTransition.settle(); sortTransition.settle()
    }
    func updateRenderScale(_ value: CGFloat) {
        scale = value.isFinite ? min(8, max(1, value)) : 2
        TelemetryArtwork.withoutActions { TelemetryArtwork.scale(layer, scale) }
        if reduceMotion() {
            TelemetryArtwork.removeAnimations(layer)
            pageTransition.settle(); sortTransition.settle()
        }
    }
    @discardableResult func mouseDown(at point: CGPoint) -> Bool {
        guard point.x.isFinite, point.y.isFinite, layer.bounds.contains(point) else { return false }
        if let action = accessibleActions.first(where: { $0.rect.contains(point) }) { perform(actionID: action.id) }
        return true
    }
    func perform(actionID: String) {
        guard active, accessibleActions.contains(where: { $0.id == actionID }) else { return }
        var changedSort = false
        if actionID.hasPrefix("activity:sort:"), let key = AppActivitySortKey(rawValue: String(actionID.dropFirst("activity:sort:".count))) {
            sortDescending = key == sortKey ? !sortDescending : key != .name
            sortKey = key; scrollOffset = 0; apps.setSort(key, descending: sortDescending)
            changedSort = true
        } else if actionID == "activity:apps" || actionID == "activity:overview" {
            let next = actionID == "activity:apps"
            guard next != isShowingApps else { return }
            isShowingApps = next
            if next { apps.setSort(sortKey, descending: sortDescending); apps.activate() } else { apps.deactivate() }
            render(animated: false)
            sortTransition.settle()
            pageTransition.select(next ? 1 : 0, direction: next ? 1 : -1, animated: !reduceMotion())
            onChange?(); return
        }
        if changedSort {
            renderApps(); onChange?()
            // A user-requested reordering moves only the rows. Live samples
            // continue updating directly without restarting an action effect.
            pageTransition.settle()
            sortTransition.reveal(direction: sortDescending ? 1 : -1, animated: !reduceMotion())
        }
    }
    func cancelDetail() -> Bool {
        guard active, isShowingApps else { return false }; perform(actionID: "activity:overview"); return true
    }
    @discardableResult func scroll(at point: CGPoint, delta: CGFloat) -> Bool {
        guard active, isShowingApps, Self.appsViewport.contains(point), delta.isFinite else { return false }
        let next = min(max(0, CGFloat(apps.snapshot.items.count) * 49 - Self.appsViewport.height), max(0, scrollOffset + delta))
        guard next != scrollOffset else { return true }
        scrollOffset = next
        renderApps(); onChange?(); return true
    }

    private func appCPU(_ value: Double?) -> String {
        guard let value, value.isFinite, value >= 0 else { return "—" }
        return String(format: "%.1f%%", value)
    }

    private func buildAppRows() {
        appLayer.frame = layer.bounds; appLayer.name = "activity.apps"
        layer.addSublayer(appLayer)
        appRows.name = "activity.apps.rows"; appRows.frame = Self.appsViewport; appRows.masksToBounds = true; appLayer.addSublayer(appRows)
        for (index, column) in appColumns.enumerated() {
            _ = HUDControlHighlightLayer.add(to: appLayer, rect: column.frame.insetBy(dx: 0, dy: -5))
            appHeaders.append(TelemetryArtwork.text("activity.apps.header.\(index)", frame: column.frame, size: 9, parent: appLayer,
                                                    weight: .medium, alignment: index == 0 ? .left : .right))
        }
        appHeaderRule.name = "activity.apps.header.rule"; appHeaderRule.frame = CGRect(x: 12, y: 80, width: 376, height: 0.5)
        appLayer.addSublayer(appHeaderRule)
        for index in 0..<6 { appRowPool.append(AppRow(index, parent: appRows)) }
        appEmpty.frame = CGRect(x: 20, y: 172, width: 360, height: 42); appEmpty.fontSize = 12
        appEmpty.alignmentMode = .center; appEmpty.isWrapped = true; appLayer.addSublayer(appEmpty)
        appScrollBar.name = "activity.apps.scrollbar"; appRows.addSublayer(appScrollBar)
        for index in 0..<2 {
            let plate = TelemetryArtwork.plate(.zero, parent: layer, name: "activity.control.\(index)")
            plate.frame = accessibleActions[index].rect
            _ = HUDControlHighlightLayer.add(to: plate, rect: plate.bounds)
            let text = TelemetryArtwork.text("activity.control.label.\(index)", frame: .zero, size: 9.5, parent: plate, weight: .medium, alignment: .center)
            controls.append((plate, text))
        }
    }

    private func renderTabs() {
        let actions = accessibleActions.prefix(2)
        for (index, pair) in controls.enumerated() {
            guard index < actions.count else { pair.0.isHidden = true; continue }
            let action = actions[index]
            pair.0.isHidden = false; pair.0.frame = action.rect
            let selected = action.id == (isShowingApps ? "activity:apps" : "activity:overview")
            pair.0.backgroundColor = (selected ? TelemetryArtwork.yellow : NSColor(white: dark ? 0.23 : 0.85, alpha: 1)).cgColor
            pair.1.foregroundColor = (selected || !dark ? NSColor(white: 0.1, alpha: 1) : TelemetryArtwork.rowText).cgColor
            pair.1.string = action.label; pair.1.frame = pair.0.bounds.insetBy(dx: 3, dy: 5)
        }
    }

    private func renderApps() {
        guard isShowingApps else { return }
        TelemetryArtwork.withoutActions {
            renderTabs()
            let items = apps.snapshot.items
            let maximum = max(0, CGFloat(items.count) * 49 - Self.appsViewport.height)
            scrollOffset = min(maximum, max(0, scrollOffset))
            let first = Int(floor(scrollOffset / 49))
            for (column, text) in zip(appColumns, appHeaders) {
                let selected = column.key == sortKey
                text.string = column.title + (selected ? (sortDescending ? " ↓" : " ↑") : "")
                text.foregroundColor = (selected ? TelemetryArtwork.primary(dark) : TelemetryArtwork.muted(dark)).cgColor
            }
            appHeaderRule.backgroundColor = TelemetryArtwork.muted(dark).withAlphaComponent(0.35).cgColor
            appEmpty.isHidden = !items.isEmpty
            appEmpty.string = L10n.text("No app readings available", "暂无应用读数")
            appEmpty.foregroundColor = TelemetryArtwork.muted(dark).cgColor
            for (offset, row) in appRowPool.enumerated() {
                let index = first + offset
                guard index < items.count else { row.plate.isHidden = true; continue }
                let item = items[index]; row.plate.isHidden = false
                row.plate.frame = CGRect(x: 0, y: CGFloat(index) * 49 - scrollOffset, width: 374, height: 45)
                TelemetryArtwork.stylePlate(row.plate, dark: dark)
                row.name.string = item.name; row.cpu.string = appCPU(item.cpuPercent)
                row.memory.string = TelemetryArtwork.bytes(item.memoryBytes.map { Double($0) })
                row.upload.string = "↑ " + TelemetryArtwork.rate(item.uploadBytesPerSecond)
                row.download.string = "↓ " + TelemetryArtwork.rate(item.downloadBytesPerSecond)
                row.read.string = L10n.text("R ", "读 ") + TelemetryArtwork.rate(item.diskReadBytesPerSecond)
                row.write.string = L10n.text("W ", "写 ") + TelemetryArtwork.rate(item.diskWriteBytesPerSecond)
                for label in [row.name, row.cpu, row.memory, row.upload, row.download, row.read, row.write] { label.foregroundColor = TelemetryArtwork.rowText.cgColor }
                for label in [row.cpu, row.upload, row.read] { label.foregroundColor = TelemetryArtwork.yellow.cgColor }
                for label in [row.memory, row.download, row.write] { label.foregroundColor = TelemetryArtwork.cyan.cgColor }
                if iconCache[item.id] == nil, let url = item.bundleURL {
                    let icon = NSWorkspace.shared.icon(forFile: url.path); icon.size = NSSize(width: 32, height: 32); iconCache[item.id] = icon
                }
                row.icon.contents = iconCache[item.id]
            }
            let liveIDs = Set(items.map(\.id)); iconCache = iconCache.filter { liveIDs.contains($0.key) }
            appScrollBar.isHidden = maximum == 0
            if maximum > 0 {
                let height = max(20, Self.appsViewport.height * Self.appsViewport.height / (CGFloat(items.count) * 49))
                appScrollBar.frame = CGRect(x: 374, y: (Self.appsViewport.height - height) * scrollOffset / maximum, width: 2, height: height)
                appScrollBar.backgroundColor = TelemetryArtwork.yellow.cgColor
            }
            TelemetryArtwork.scale(appLayer, scale)
        }
    }

    private func render(animated: Bool) {
        let value = controller.snapshot, history = Array(controller.history.suffix(60))
        func finitePeak(_ values: [Double?]) -> Double { max(1, values.compactMap { $0 }.filter { $0.isFinite && $0 >= 0 }.max().map { $0 * 1.12 } ?? 1) }
        let cpu = history.map(\.cpuPercent)
        let memory: [Double?] = history.map { sample in
            guard let used = sample.memoryUsedBytes, let total = sample.memoryTotalBytes, total > 0 else { return nil }
            return Double(used) / Double(total) * 100
        }
        let upload = history.map(\.uploadBytesPerSecond), download = history.map(\.downloadBytesPerSecond)
        let read = history.map(\.diskReadBytesPerSecond), write = history.map(\.diskWriteBytesPerSecond)
        TelemetryArtwork.withoutActions {
            heading.string = HUDModule.activityMonitor.title; heading.foregroundColor = TelemetryArtwork.primary(dark).cgColor
            subtitle.string = ""; subtitle.isHidden = true; footer.string = ""; footer.isHidden = true
            renderTabs()
            subtitle.foregroundColor = TelemetryArtwork.muted(dark).cgColor
            let labels = ["CPU", L10n.text("Memory", "内存"), L10n.text("Network", "网络"), L10n.text("Disk I/O", "磁盘 I/O")]
            let captions = [L10n.text("TOTAL LOAD", "总负载"), L10n.text("USED / TOTAL", "已用 / 总计"), L10n.text("UP / DOWN", "上传 / 下载"), L10n.text("READ / WRITE", "读取 / 写入")]
            for (index, row) in rows.enumerated() {
                let colors = index == 1 ? [ActivityGraphPalette.blue] : index >= 2
                    ? [ActivityGraphPalette.yellow, ActivityGraphPalette.blue] : [ActivityGraphPalette.yellow]
                row.tick.backgroundColor = colors[0].cgColor
                row.graph.setColors(colors)
                TelemetryArtwork.stylePlate(row.layer, dark: dark)
                row.title.string = labels[index]; row.subtitle.string = captions[index]
                row.title.foregroundColor = TelemetryArtwork.rowText.cgColor; row.subtitle.foregroundColor = TelemetryArtwork.rowMuted.cgColor
                row.primary.foregroundColor = (index == 1 ? ActivityGraphPalette.blue : ActivityGraphPalette.yellow).cgColor
                row.secondary.foregroundColor = (index >= 2 ? ActivityGraphPalette.blue : TelemetryArtwork.rowMuted).cgColor
                row.tertiary.foregroundColor = TelemetryArtwork.rowMuted.cgColor
                row.tertiary.string = ""
            }
            rows[0].primary.string = TelemetryArtwork.percent(value.cpuPercent)
            rows[0].secondary.string = "0–100%"
            rows[1].primary.string = TelemetryArtwork.bytes(value.memoryUsedBytes.map { Double($0) })
            rows[1].secondary.string = "/ " + TelemetryArtwork.bytes(value.memoryTotalBytes.map { Double($0) })
            rows[1].tertiary.string = L10n.text("Compressed ", "压缩 ") + TelemetryArtwork.bytes(value.memoryCompressedBytes.map { Double($0) })
            rows[2].primary.string = "↑ " + TelemetryArtwork.rate(value.uploadBytesPerSecond)
            rows[2].secondary.string = "↓ " + TelemetryArtwork.rate(value.downloadBytesPerSecond)
            rows[3].primary.string = L10n.text("R ", "读 ") + TelemetryArtwork.rate(value.diskReadBytesPerSecond)
            rows[3].secondary.string = L10n.text("W ", "写 ") + TelemetryArtwork.rate(value.diskWriteBytesPerSecond)
            rows[0].graph.update(series: [cpu], ceiling: 100, animated: animated, timestamps: history.map(\.uptime))
            rows[1].graph.update(series: [memory], ceiling: 100, animated: animated, timestamps: history.map(\.uptime))
            rows[2].graph.update(series: [upload, download], ceiling: finitePeak(upload + download), animated: animated, timestamps: history.map(\.uptime))
            rows[3].graph.update(series: [read, write], ceiling: finitePeak(read + write), animated: animated, timestamps: history.map(\.uptime))
            if isShowingApps { renderApps() }
            TelemetryArtwork.scale(layer, scale)
        }
    }
}
