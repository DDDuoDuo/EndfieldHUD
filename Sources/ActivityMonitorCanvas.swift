import AppKit
import QuartzCore

/// Activity Monitor (spec §3.25): Overview keeps the v1.2.0 row layout (name,
/// live graph, value) in the new style with GPU first; Apps is a sortable
/// table. SystemActivityMonitor keeps the session's history at all times;
/// this canvas asks for 1 Hz samples (and app readings, on Apps) only while
/// it is the active module, and updates every value in place.
final class ActivityMonitorCanvas: HUDDesignCanvas {
    /// The redesigned canvas has no telemetry-owned loops; the host's
    /// diagnostics still ask (SystemHUDView.telemetryAnimationCount).
    var animationCount: Int { 0 }
    private let controller: SystemActivityMonitor
    private let apps: AppActivityMonitor
    private var observer: UUID?
    private var appObserver: UUID?
    private(set) var isShowingApps = false
    private(set) var sortKey: AppActivitySortKey = .cpu
    private(set) var sortDescending = true
    private var rows: [ActivityRow] = []
    private var table: AppTable?
    /// App icons outlive the Apps page (kept across visits and sorts).
    private let icons = ActivityAppIcons()
    /// The tab being left: the next page's active tab opens from it (`tabOn`).
    private var leavingTab: Int?

    init(context: HUDDesignContext, controller: SystemActivityMonitor, apps: AppActivityMonitor = .fixture()) {
        self.controller = controller; self.apps = apps
        super.init(module: .activityMonitor, context: context)
    }

    deinit {
        if let observer { controller.removeObserver(observer) }
        if let appObserver { apps.removeObserver(appObserver) }
        if isActive { controller.deactivate(); apps.deactivate() }
    }

    // MARK: Lifecycle

    override func didActivate() {
        if observer == nil {
            observer = controller.observe { [weak self] _ in
                guard let self, !self.isShowingApps else { return }
                self.updateOverview()
            }
        }
        if appObserver == nil {
            appObserver = apps.observe { [weak self] _ in
                guard let self, self.isShowingApps else { return }
                self.updateApps()
            }
        }
        controller.activate()
        if isShowingApps { apps.setSort(sortKey, descending: sortDescending); apps.activate() }
    }

    override func didDeactivate() { stopReading() }
    override func willLeave() { stopReading() }

    private func stopReading() {
        if let observer { controller.removeObserver(observer) }
        if let appObserver { apps.removeObserver(appObserver) }
        observer = nil; appObserver = nil
        controller.deactivate(); apps.deactivate()
    }

    // MARK: Actions

    /// `activity:overview`, `activity:apps` and `activity:sort:<key>`.
    func perform(actionID: String) {
        if actionID == "activity:overview" || actionID == "activity:apps" {
            let next = actionID == "activity:apps"
            guard next != isShowingApps else { return }
            page.tabs?.select(next ? 1 : 0, notify: true)
        } else if actionID.hasPrefix("activity:sort:"),
                  let key = AppActivitySortKey(rawValue: String(actionID.dropFirst("activity:sort:".count))) {
            sort(by: key)
        }
    }

    private func showApps(_ next: Bool, direction: CGFloat) {
        guard next != isShowingApps else { return }
        leavingTab = isShowingApps ? 1 : 0
        isShowingApps = next
        if next {
            apps.setSort(sortKey, descending: sortDescending)
            if isActive { apps.activate() }
        } else { apps.deactivate() }
        navigate(dx: direction)
    }

    /// Spec §3.25: the active column flips; a new one starts descending,
    /// except App (A–Z). The rows then re-stagger in from below.
    private func sort(by key: AppActivitySortKey) {
        guard isShowingApps, let table else { return }
        sortDescending = key == sortKey ? !sortDescending : key != .name
        sortKey = key
        apps.setSort(key, descending: sortDescending)
        updateApps()
        HUDMotion.stagger(table.visibleRows.map(\.plate.layer), from: CGPoint(x: 0, y: 12), env: env)
    }

    override func handleScroll(at point: CGPoint, delta: CGPoint, event: NSEvent?) -> Bool {
        guard isShowingApps, let table, table.area.layer.frame.contains(point) else { return false }
        if table.area.scroll(by: delta.y) {
            table.bind(sortKey: sortKey)
            HUDControlHighlightLayer.requestRefresh(on: context.host)
        }
        return true
    }

    // MARK: Pages

    override func buildPage(_ page: HUDDesignPage) {
        rows = []; table = nil
        let selected = isShowingApps ? 1 : 0
        let tabs = HUDTabs(id: "activityTabs", titles: [(L10n.text("Overview", "概览"), nil), (L10n.text("Apps", "应用"), nil)],
                           selectedIndex: leavingTab ?? selected, width: HUDMetric.box.width, env: env)
        tabs.select(selected, notify: false)
        leavingTab = nil
        tabs.onSelect = { [weak self] index, direction in self?.showApps(index == 1, direction: direction) }
        HUDMotion.perform { tabs.layer.frame.origin = CGPoint(x: 0, y: HUDMetric.headerHeight); page.layer.addSublayer(tabs.layer) }
        page.add(tabs.controls); page.tabs = tabs; page.stagger(tabs.layer)
        if isShowingApps { buildApps(page) } else { buildOverview(page) }
        env.applyScale(to: page.layer)
    }

    private func buildOverview(_ page: HUDDesignPage) {
        header.configure(crumbs: ["ACTIVITY"], code: "SYS-11", title: HUDModuleTitle("Activity", "活动监视器"),
                         counter: "LIVE", showsBack: false)
        rows = ActivityRow.Metric.allCases.map { ActivityRow(metric: $0, env: env) }
        HUDMotion.perform {
            for (index, row) in rows.enumerated() {
                row.layer.frame.origin = CGPoint(x: 0, y: 125 + CGFloat(index) * (ActivityRow.size.height + HUDMetric.rowGap))
                page.layer.addSublayer(row.layer)
            }
        }
        rows.forEach { page.add($0); page.stagger($0.layer) }
        updateOverview()
    }

    private func updateOverview() {
        guard !rows.isEmpty else { return }
        let history = Array(controller.history.suffix(ActivityGraph.samples))
        let value = controller.snapshot
        for row in rows { row.update(history: history, latest: value) }
    }

    private func buildApps(_ page: HUDDesignPage) {
        header.configure(crumbs: ["ACTIVITY"], code: "SYS-11", title: HUDModuleTitle("Activity", "活动监视器"),
                         counter: HUDFormat.pad2(apps.snapshot.items.count), showsBack: false)
        let columns = AppTable.columns.map { column in
            SortColumn(key: column.key, title: column.title, frame: CGRect(x: column.x, y: 133, width: column.width, height: 10),
                       alignment: column.key == .name ? .left : .right, env: env)
        }
        let head = HUDLayer.plain("activity.columns")
        HUDMotion.perform {
            head.frame = CGRect(x: 0, y: 0, width: HUDMetric.box.width, height: HUDMetric.box.height)
            columns.forEach { head.addSublayer($0.layer) }
            page.layer.addSublayer(head)
        }
        for column in columns {
            column.onActivate = { [weak self] in self?.sort(by: column.key) }
            page.add(column)
        }
        let area = page.scrollArea(id: "apps", frame: CGRect(x: 0, y: 155, width: HUDMetric.box.width, height: 330))
        let table = AppTable(area: area, columns: columns, icons: icons, env: env)
        self.table = table
        icons.onLoad = { [weak table] ids in table?.showIcons(ids) }
        page.stagger(head)
        table.update(items: apps.snapshot.items, waiting: apps.snapshot.timestamp == .distantPast,
                     sortKey: sortKey, descending: sortDescending)
        page.restoreOffset(of: area)
        table.bind(sortKey: sortKey)
        table.rows.forEach { page.add($0.plate); page.add($0.readout) }
        table.visibleRows.forEach { page.stagger($0.plate.layer) }
        table.onRowsAdded = { [weak self] added in
            guard let self, self.table === table else { return }
            added.forEach { self.page.add($0.plate); self.page.add($0.readout) }
            self.refreshRouter()
        }
    }

    private func updateApps() {
        guard let table else { return }
        let snapshot = apps.snapshot
        header.setCounter(HUDFormat.pad2(snapshot.items.count))
        table.update(items: snapshot.items, waiting: snapshot.timestamp == .distantPast, sortKey: sortKey, descending: sortDescending)
        table.bind(sortKey: sortKey)
    }
}

/// The Overview graph (spec §3.25): the last 44 samples across the row's
/// graph box with the #2e2e2e grid, a .35 (secondary .25) area and a 1.6 px
/// line per series. Missing readings break the trace; they never become a
/// fabricated zero or a line across the gap.
final class ActivityGraph {
    static let samples = 44
    static let size = CGSize(width: 296, height: 44)
    let layer = HUDLayer.configure(HUDDecorativeContentLayer(), "activity.graph")
    private var traces: [(area: CAShapeLayer, line: CAShapeLayer)] = []

    /// `colors` from back to front: the secondary series first.
    init(colors: [(color: CGColor, areaOpacity: Float)]) {
        HUDMotion.perform {
            layer.frame = CGRect(origin: .zero, size: Self.size)
            let grid = HUDLayer.shape("activity.graph.grid")
            grid.frame = layer.bounds
            let path = CGMutablePath()
            for index in 1...4 {
                path.addRect(CGRect(x: (Self.size.width * CGFloat(index) / 5).rounded() - 0.5, y: 0, width: 1, height: Self.size.height))
            }
            path.addRect(CGRect(x: 0, y: Self.size.height / 2 - 0.5, width: Self.size.width, height: 1))
            grid.path = path; grid.fillColor = HUDColor.tone(0x2E2E2E)
            layer.addSublayer(grid)
            for entry in colors {
                let area = HUDLayer.shape(), line = HUDLayer.shape()
                area.frame = layer.bounds; line.frame = layer.bounds
                area.fillColor = entry.color; area.opacity = entry.areaOpacity
                line.fillColor = nil; line.strokeColor = entry.color; line.lineWidth = 1.6
                line.lineJoin = .round; line.lineCap = .round
                layer.addSublayer(area); layer.addSublayer(line)
                traces.append((area, line))
            }
        }
    }

    /// Values in 0–100, oldest first, matching the init's series order.
    func update(_ series: [[Double?]]) {
        HUDMotion.perform {
            for (trace, values) in zip(traces, series) {
                let paths = Self.paths(values, size: Self.size)
                trace.area.path = paths.area; trace.line.path = paths.line
            }
        }
    }

    /// The newest sample sits at the right edge; fewer than 44 samples leave
    /// the left of the box empty. y = h − v × .42 (v 0–100) as in the demo.
    static func paths(_ values: [Double?], size: CGSize) -> (area: CGPath, line: CGPath) {
        let window = Array(values.suffix(samples))
        let area = CGMutablePath(), line = CGMutablePath()
        var run: [CGPoint] = []
        func flush() {
            defer { run = [] }
            guard run.count > 1, let first = run.first, let last = run.last else { return }
            line.addLines(between: run)
            area.addLines(between: [CGPoint(x: first.x, y: size.height)] + run + [CGPoint(x: last.x, y: size.height)])
            area.closeSubpath()
        }
        let offset = samples - window.count
        for (index, value) in window.enumerated() {
            guard let value, value.isFinite else { flush(); continue }
            let x = size.width * CGFloat(offset + index) / CGFloat(samples - 1)
            let y = size.height - CGFloat(min(100, max(0, value))) * 0.42 * size.height / 44
            run.append(CGPoint(x: x, y: y))
        }
        flush()
        return (area, line)
    }
}

/// One Overview row (spec §3.25): dark chamfered plate, colour bar, name and
/// caption, the live graph and a single- or two-line readout.
final class ActivityRow: HUDControl {
    enum Metric: Int, CaseIterable { case gpu, cpu, ram, network, disk }
    static let size = CGSize(width: HUDMetric.box.width, height: 64)
    let metric: Metric
    private let brighten = HUDLayer.shape("activity.row.brighten")
    private let sheen = HUDSheen(alpha: 0.07)
    private let graph: ActivityGraph
    private let primary: HUDTextLayer
    private let secondary: HUDTextLayer
    private(set) var readoutLines: [String] = []

    /// Read when the row is built: the accent follows the theme colour, and
    /// both take their light-surface variants in the light theme.
    private static var yellow: CGColor { HUDColor.accentInk }
    private static var cyan: CGColor { HUDColor.legible(HUDColor.cyan) }

    init(metric: Metric, env: HUDDesignEnvironment) {
        self.metric = metric
        let color = metric == .ram || metric == .network ? Self.cyan : Self.yellow
        let secondaryColor = metric == .network ? Self.yellow : Self.cyan
        graph = ActivityGraph(colors: (metric.rawValue >= 3 ? [(secondaryColor, Float(0.25))] : []) + [(color, Float(0.35))])
        let two = metric.rawValue >= 3
        let top = HUDTextStyle(size: two ? 14 : 19, weight: .regular, color: two ? Self.yellow : color,
                               lineHeight: two ? 1.45 : 1.1, tabular: true)
        let bottom = two ? top.colored(Self.cyan) : HUDTextStyle(size: 10.5, weight: .regular, color: HUDColor.steel, lineHeight: 1.5)
        primary = HUDTextLayer(style: top, alignment: .right)
        secondary = HUDTextLayer(style: bottom, alignment: .right)
        let names: [String] = ["GPU", "CPU", L10n.text("RAM", "RAM"), L10n.text("Network", "网络"), L10n.text("Disk I/O", "磁盘 I/O")]
        let captions = [L10n.text("TOTAL LOAD", "总负载"), L10n.text("TOTAL LOAD", "总负载"), L10n.text("USED / TOTAL", "已用 / 总计"),
                        L10n.text("UP / DOWN", "上传 / 下载"), L10n.text("READ / WRITE", "读取 / 写入")]
        super.init(id: "activity.row.\(metric.rawValue)", label: names[metric.rawValue], env: env)
        let bounds = CGRect(origin: .zero, size: Self.size)
        let shape = HUDShape.chamfer(bounds, cut: HUDMetric.rowChamfer)
        HUDMotion.perform {
            layer.bounds = bounds
            let plate = HUDLayer.plain("activity.row.plate")
            plate.frame = bounds
            plate.showArtwork(HUDArtwork.plate([HUDColor.tone(0x232323), HUDColor.tone(0x1A1A1A)], size: bounds.size,
                                               cut: HUDMetric.rowChamfer, scale: env.renderScale), scale: env.renderScale)
            // The inset ring follows the box; the chamfer clips its corner.
            let ring = HUDLayer.shape("activity.row.ring")
            let edge = CGMutablePath(), cut = HUDMetric.rowChamfer
            edge.move(to: CGPoint(x: bounds.maxX - cut, y: 0.5)); edge.addLine(to: CGPoint(x: 0.5, y: 0.5))
            edge.addLine(to: CGPoint(x: 0.5, y: bounds.maxY - 0.5)); edge.addLine(to: CGPoint(x: bounds.maxX - 0.5, y: bounds.maxY - 0.5))
            edge.addLine(to: CGPoint(x: bounds.maxX - 0.5, y: cut))
            ring.frame = bounds; ring.path = edge; ring.fillColor = nil
            ring.strokeColor = HUDColor.ringTab; ring.lineWidth = 1
            brighten.frame = bounds; brighten.path = shape; brighten.fillColor = HUDColor.foreground(0.025); brighten.opacity = 0
            sheen.setShape(shape, bounds: bounds)
            let bar = HUDLayer.plain("activity.row.bar")
            bar.frame = CGRect(x: 0, y: 10, width: 3, height: bounds.height - 20); bar.backgroundColor = color
            let name = HUDTextLayer(names[metric.rawValue], style: HUDTextStyle(size: 15, weight: .medium, color: HUDColor.tone(0xF2F2F2),
                                                                                  lineHeight: 1.15))
            name.lineFrame = CGRect(x: 18, y: 15.5, width: 118, height: 17.25)
            let caption = HUDTextLayer(captions[metric.rawValue], style: HUDTextStyle(size: 9.5, weight: .regular, color: HUDColor.steel,
                                                                                        tracking: 0.1, uppercase: true))
            caption.lineFrame = CGRect(x: 18, y: 37.75, width: 118, height: 9.5)
            graph.layer.frame.origin = CGPoint(x: 148, y: 10)
            if two {
                primary.lineFrame = CGRect(x: 456, y: 11.7, width: 120, height: 20.3)
                secondary.lineFrame = CGRect(x: 456, y: 32, width: 120, height: 20.3)
            } else {
                primary.lineFrame = CGRect(x: 456, y: 13.6, width: 120, height: 20.9)
                secondary.lineFrame = CGRect(x: 456, y: 34.5, width: 120, height: 15.75)
            }
            [plate, ring, brighten, sheen.layer, bar, name, caption, graph.layer, primary, secondary].forEach(layer.addSublayer)
        }
        setTarget(bounds, path: shape)
    }

    /// Paths and readouts from the retained history (oldest first) and the latest sample.
    func update(history: [SystemActivitySnapshot], latest value: SystemActivitySnapshot) {
        func peak(_ series: [[Double?]]) -> Double {
            max(1000, (series.flatMap { $0 }.compactMap { $0 }.filter { $0.isFinite && $0 >= 0 }.max() ?? 0) * 1.12)
        }
        func scaled(_ values: [Double?], _ ceiling: Double) -> [Double?] { values.map { $0.map { $0 / ceiling * 100 } } }
        let lines: [String]
        switch metric {
        case .gpu, .cpu:
            let path: KeyPath<SystemActivitySnapshot, Double?> = metric == .gpu ? \.gpuPercent : \.cpuPercent
            graph.update([history.map { $0[keyPath: path] }])
            lines = [TelemetryFormat.percent(value[keyPath: path]), "0–100%"]
        case .ram:
            graph.update([history.map { sample in
                guard let used = sample.memoryUsedBytes, let total = sample.memoryTotalBytes, total > 0 else { return nil }
                return Double(used) / Double(total) * 100
            }])
            lines = [TelemetryFormat.bytes(value.memoryUsedBytes.map { Double($0) }),
                     "/ " + TelemetryFormat.bytes(value.memoryTotalBytes.map { Double($0) })]
        case .network:
            let up = history.map(\.uploadBytesPerSecond), down = history.map(\.downloadBytesPerSecond)
            let ceiling = peak([up, down])
            graph.update([scaled(up, ceiling), scaled(down, ceiling)])
            lines = ["↑ " + TelemetryFormat.rate(value.uploadBytesPerSecond), "↓ " + TelemetryFormat.rate(value.downloadBytesPerSecond)]
        case .disk:
            let read = history.map(\.diskReadBytesPerSecond), write = history.map(\.diskWriteBytesPerSecond)
            let ceiling = peak([read, write])
            graph.update([scaled(write, ceiling), scaled(read, ceiling)])
            lines = [L10n.text("R ", "读 ") + TelemetryFormat.rate(value.diskReadBytesPerSecond),
                     L10n.text("W ", "写 ") + TelemetryFormat.rate(value.diskWriteBytesPerSecond)]
        }
        guard lines != readoutLines else { return }
        readoutLines = lines
        HUDMotion.perform { primary.text = lines[0]; secondary.text = lines[1] }
        env.applyScale(to: primary); env.applyScale(to: secondary)
    }

    override func hoverChanged() {
        let on = isHovered && isEnabled
        HUDMotion.animate(layer, "transform.translation.x", to: on ? 2 : 0, duration: HUDDuration.hover, env: env)
        HUDMotion.animate(brighten, "opacity", to: Float(on ? 1 : 0), duration: HUDDuration.hover, timing: HUDEasing.ease, env: env)
        sheen.play(on, env: env)
    }

    override func pointerDown(at point: CGPoint) -> Bool { false }
    override var accessibilityRole: HUDAccessibilityItem.Role? { .text(readoutLines.joined(separator: ", ")) }
}

/// An Apps column header (spec §3.25): steel, the active one yellow with ▼/▲.
final class SortColumn: HUDControl {
    let key: AppActivitySortKey
    private let title: String
    private let text: HUDTextLayer
    private(set) var isActiveColumn = false
    private(set) var isDescending = true

    init(key: AppActivitySortKey, title: String, frame: CGRect, alignment: HUDTextLayer.Alignment, env: HUDDesignEnvironment) {
        self.key = key; self.title = title
        text = HUDTextLayer(title, style: HUDTextStyle(size: 10, weight: .regular, color: HUDColor.steel, tracking: 0.08, uppercase: true),
                            alignment: alignment)
        super.init(id: "activity:sort:" + key.rawValue, label: title, env: env)
        HUDMotion.perform {
            layer.frame = frame.insetBy(dx: 0, dy: -8)
            text.lineFrame = CGRect(x: 0, y: 8, width: frame.width, height: frame.height)
            layer.addSublayer(text)
        }
        setTarget(layer.bounds)
    }

    func render(active: Bool, descending: Bool) {
        isActiveColumn = active; isDescending = descending
        HUDMotion.perform {
            text.runs = [HUDText.Run(title + (active ? (descending ? " ▼" : " ▲") : ""),
                                     text.textStyle.colored(active ? HUDColor.accentInk : HUDColor.steel))]
        }
        label = L10n.text("Sort by ", "排序：") + title
            + (active ? (descending ? L10n.text(", descending", "，降序") : L10n.text(", ascending", "，升序")) : "")
        env.applyScale(to: text)
    }
}

/// The Apps table body: a scroll area of light rows (spec §3.25) virtualised
/// over a small pool, so a long app list never means hundreds of layers for
/// the hover pass. Rows are rebound to items by index on samples and scrolls.
final class AppTable {
    struct Column { let key: AppActivitySortKey; let title: String; let x: CGFloat; let width: CGFloat }
    static var columns: [Column] { [Column(key: .name, title: L10n.text("App", "应用"), x: 18, width: 274),
                          Column(key: .gpu, title: "GPU", x: 300, width: 60),
                          Column(key: .cpu, title: "CPU", x: 368, width: 60),
                          Column(key: .memory, title: L10n.text("RAM", "RAM"), x: 436, width: 66),
                          Column(key: .threads, title: L10n.text("Threads", "线程"), x: 510, width: 62)] }
    static let rowHeight: CGFloat = 40
    static var pitch: CGFloat { rowHeight + HUDMetric.rowGap }
    static let rowWidth: CGFloat = 584

    final class Row {
        let plate: HUDRowPlate
        let readout: HUDReadout
        /// The app's own icon, or the generic tile for an app without one.
        fileprivate let icon = HUDLayer.plain("activity.app.icon")
        fileprivate let tile = HUDLayer.plain("activity.app.tile")
        fileprivate let name: HUDTextLayer
        fileprivate let numbers: [HUDTextLayer]
        fileprivate var item: AppActivityItem?
        fileprivate var sortKey: AppActivitySortKey?
        /// The app whose icon the row shows (rebinding to another app swaps it).
        fileprivate var iconID: String?

        init(index: Int, env: HUDDesignEnvironment) {
            plate = HUDRowPlate(id: "activity.app.\(index)", title: "", variant: .compact, width: AppTable.rowWidth,
                                height: AppTable.rowHeight, env: env)
            readout = HUDReadout(id: "activity.app.\(index).readout", label: "", frame: plate.layer.bounds, env: env)
            name = HUDTextLayer(style: HUDTextStyle(size: 13.5, weight: .medium, color: HUDColor.ink))
            numbers = AppTable.columns.dropFirst().map { _ in
                HUDTextLayer(style: HUDTextStyle(size: 11.5, weight: .bold, color: HUDColor.rgb(0x555555), tabular: true), alignment: .right)
            }
            HUDMotion.perform {
                let frame = CGRect(x: 18, y: 11, width: ActivityAppIcons.side, height: ActivityAppIcons.side)
                icon.frame = frame; icon.contentsGravity = .resizeAspect; icon.isHidden = true
                tile.frame = frame; tile.backgroundColor = HUDColor.rgb(0x1B1B1B); tile.isHidden = true
                let glyph = HUDIconLayer(.apps, size: 12, color: HUDColor.yellow)
                glyph.position = CGPoint(x: frame.width / 2, y: frame.height / 2); tile.addSublayer(glyph)
                name.lineFrame = CGRect(x: 46, y: 14.25, width: 246, height: 13.5)
                for (column, number) in zip(AppTable.columns.dropFirst(), numbers) {
                    number.lineFrame = CGRect(x: column.x, y: 15.25, width: column.width, height: 11.5)
                }
                ([tile, icon, name] + numbers + [readout.layer]).forEach(plate.layer.addSublayer)
            }
        }

        /// Sets the icon when the row shows another app or its icon arrived;
        /// a pending icon leaves the slot empty until it does.
        fileprivate func showIcon(_ value: ActivityAppIcons.Icon, id: String) {
            iconID = value == .pending ? nil : id
            HUDMotion.perform {
                if case .image(let image) = value { icon.contents = image } else { icon.contents = nil }
                icon.isHidden = icon.contents == nil
                tile.isHidden = value != .generic
            }
        }

        fileprivate func show(_ item: AppActivityItem, sortKey: AppActivitySortKey, icons: ActivityAppIcons, env: HUDDesignEnvironment) {
            if iconID != item.id { showIcon(icons.icon(for: item), id: item.id) }
            guard item != self.item || sortKey != self.sortKey else { return }
            self.item = item; self.sortKey = sortKey
            let values = [TelemetryFormat.percent(item.gpuPercent), TelemetryFormat.percent(item.cpuPercent, limit: nil),
                          TelemetryFormat.memory(item.memoryBytes), item.threads.map { String($0) } ?? TelemetryFormat.missing]
            HUDMotion.perform {
                name.text = item.name
                for (index, number) in numbers.enumerated() {
                    let sorted = AppTable.columns[index + 1].key == sortKey
                    number.runs = [HUDText.Run(values[index], number.textStyle.colored(sorted ? HUDColor.nearBlack : HUDColor.rgb(0x555555)))]
                }
            }
            readout.label = item.name
            readout.value = zip(AppTable.columns.dropFirst().map(\.title), values).map { $0 + " " + $1 }.joined(separator: ", ")
            env.applyScale(to: plate.layer)
        }
    }

    let area: HUDScrollArea
    private let columns: [SortColumn]
    private let icons: ActivityAppIcons
    private let env: HUDDesignEnvironment
    private(set) var rows: [Row] = []
    private(set) var items: [AppActivityItem] = []
    private var empty: CALayer?
    /// New pooled rows need routing (hover, accessibility).
    var onRowsAdded: (([Row]) -> Void)?
    static let poolSize = Int((330 / pitch).rounded(.up)) + 1

    init(area: HUDScrollArea, columns: [SortColumn], icons: ActivityAppIcons, env: HUDDesignEnvironment) {
        self.area = area; self.columns = columns; self.icons = icons; self.env = env
        icons.scale = env.renderScale
    }

    var visibleRows: [Row] { rows.filter { !$0.plate.layer.isHidden } }

    func update(items next: [AppActivityItem], waiting: Bool, sortKey: AppActivitySortKey, descending: Bool) {
        items = next
        columns.forEach { $0.render(active: $0.key == sortKey, descending: descending) }
        area.setContentHeight(max(0, CGFloat(items.count) * Self.pitch - HUDMetric.rowGap))
        let wanted = min(Self.poolSize, items.count)
        if rows.count < wanted {
            let added = (rows.count..<wanted).map { Row(index: $0, env: env) }
            HUDMotion.perform { added.forEach { area.content.addSublayer($0.plate.layer) } }
            rows += added
            onRowsAdded?(added)
        }
        let showsEmpty = items.isEmpty && !waiting
        if showsEmpty, empty == nil {
            let state = HUDEmptyState.bilingual("NO APP READINGS", "暂无应用读数", size: CGSize(width: HUDMetric.box.width, height: 250), env: env)
            HUDMotion.perform { state.frame.origin = .zero; area.content.addSublayer(state) }
            empty = state
        } else if !showsEmpty, let state = empty {
            HUDMotion.perform { state.removeFromSuperlayer() }; empty = nil
        }
    }

    /// Places the pool over the visible slice of `items`.
    func bind(sortKey: AppActivitySortKey) {
        let first = max(0, Int(floor(area.offset / Self.pitch)))
        HUDMotion.perform {
            for (offset, row) in rows.enumerated() {
                let index = first + offset
                guard index < items.count else { row.plate.layer.isHidden = true; continue }
                row.plate.layer.isHidden = false
                let origin = CGPoint(x: 0, y: CGFloat(index) * Self.pitch)
                if row.plate.layer.frame.origin != origin { row.plate.layer.frame.origin = origin }
            }
        }
        for (offset, row) in rows.enumerated() where first + offset < items.count {
            row.show(items[first + offset], sortKey: sortKey, icons: icons, env: env)
        }
    }

    /// Icons that finished loading go to the visible rows showing their apps.
    func showIcons(_ ids: Set<String>) {
        for row in rows where !row.plate.layer.isHidden {
            guard let item = row.item, ids.contains(item.id), row.iconID != item.id else { continue }
            row.showIcon(icons.icon(for: item), id: item.id)
        }
    }
}

/// App icons for the Apps table, rasterised once at the row's 20 px (× the
/// render scale) and kept by app (an item's bundle path). A telemetry tick
/// only looks them up: a miss is queued and loaded on a later main-queue
/// pass, a few at a time, with the system icon of the app's bundle (the
/// catalog already resolved helpers to their outer app, so no bundle walk).
/// An app without a bundle or an icon gets the generic tile.
final class ActivityAppIcons {
    enum Icon: Equatable { case image(CGImage), generic, pending }
    static let side: CGFloat = 20
    /// Bundles resolved per main-queue pass, so no pass stalls a frame.
    static let batch = 4
    static let capacity = 256
    var scale: CGFloat = 1 { didSet { if scale != oldValue { images.removeAll(); queue.removeAll() } } }
    /// Called with the ids whose icons just loaded.
    var onLoad: ((Set<String>) -> Void)?
    /// nil: resolved without an icon (generic).
    private var images: [String: CGImage?] = [:]
    private var queue: [(id: String, url: URL)] = []
    private var scheduled = false
    private(set) var loadCount = 0

    func icon(for item: AppActivityItem) -> Icon {
        guard let url = item.bundleURL else { return .generic }
        if let cached = images[item.id] { return cached.map(Icon.image) ?? .generic }
        if !queue.contains(where: { $0.id == item.id }) { queue.append((item.id, url)) }
        schedule()
        return .pending
    }

    private func schedule() {
        guard !scheduled, !queue.isEmpty else { return }
        scheduled = true
        DispatchQueue.main.async { [weak self] in self?.load() }
    }

    private func load() {
        scheduled = false
        let next = queue.prefix(Self.batch)
        queue.removeFirst(next.count)
        if images.count + next.count > Self.capacity { images.removeAll(keepingCapacity: true) }
        for entry in next {
            images[entry.id] = raster(NSWorkspace.shared.icon(forFile: entry.url.path))
            loadCount += 1
        }
        onLoad?(Set(next.map(\.id)))
        schedule()
    }

    /// One bitmap at the row's size: Core Animation never rescales a large icon representation.
    private func raster(_ image: NSImage) -> CGImage? {
        let pixels = Int((Self.side * max(1, scale)).rounded(.up))
        guard let space = CGColorSpace(name: CGColorSpace.sRGB),
              let ctx = CGContext(data: nil, width: pixels, height: pixels, bitsPerComponent: 8, bytesPerRow: 0, space: space,
                                  bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { return nil }
        NSGraphicsContext.saveGraphicsState(); defer { NSGraphicsContext.restoreGraphicsState() }
        NSGraphicsContext.current = NSGraphicsContext(cgContext: ctx, flipped: false)
        NSGraphicsContext.current?.imageInterpolation = .high
        image.draw(in: NSRect(x: 0, y: 0, width: pixels, height: pixels))
        return ctx.makeImage()
    }
}
