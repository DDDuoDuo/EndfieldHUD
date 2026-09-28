import AppKit
import QuartzCore

struct TelemetryCanvasAction {
    let id: String
    let label: String
    let rect: CGRect
}

enum TelemetryArtwork {
    static var yellow: NSColor { HUDRuntimeAppearance.accent }
    static var cyan: NSColor { HUDRuntimeAppearance.accent.blended(withFraction: 0.42, of: .white) ?? HUDRuntimeAppearance.accent }
    static let rowText = NSColor(white: 0.94, alpha: 1)
    static let rowMuted = NSColor(white: 0.65, alpha: 1)
    static func primary(_ dark: Bool) -> NSColor { NSColor(white: dark ? 0.93 : 0.12, alpha: 1) }
    static func muted(_ dark: Bool) -> NSColor { NSColor(white: dark ? 0.65 : 0.40, alpha: 1) }
    static func text(_ name: String, frame: CGRect, size: CGFloat, parent: CALayer,
                     weight: NSFont.Weight = .regular, alignment: CATextLayerAlignmentMode = .left, wrapped: Bool = false) -> CATextLayer {
        let text = CATextLayer(); text.name = name; text.frame = frame
        text.font = NSFont.systemFont(ofSize: size, weight: weight); text.fontSize = size
        text.alignmentMode = alignment; text.isWrapped = wrapped; text.truncationMode = .end
        text.actions = ["contents": NSNull()]; parent.addSublayer(text); return text
    }
    static func plate(_ frame: CGRect, parent: CALayer, name: String) -> CALayer {
        let result = CALayer(); result.name = name; result.frame = frame
        result.cornerRadius = 3; result.allowsGroupOpacity = false; parent.addSublayer(result); return result
    }
    static func stylePlate(_ layer: CALayer, dark: Bool) {
        // Charcoal report strips deliberately retain contrast in both themes.
        layer.backgroundColor = NSColor(white: dark ? 0.12 : 0.19, alpha: 1).cgColor
        layer.borderColor = NSColor(white: dark ? 0.48 : 0.30, alpha: 0.38).cgColor
        layer.borderWidth = 0.5
    }
    static func scale(_ root: CALayer, _ value: CGFloat) {
        root.contentsScale = HUDRenderScale.contentScale(for: root, baseScale: value)
        root.sublayers?.forEach { scale($0, value) }
        if let mask = root.mask { scale(mask, value) }
    }
    static func withoutActions(_ body: () -> Void) { CATransaction.begin(); CATransaction.setDisableActions(true); body(); CATransaction.commit() }
    static func removeAnimations(_ root: CALayer) {
        for key in root.animationKeys() ?? [] where key.hasPrefix("telemetry.") { root.removeAnimation(forKey: key) }
        root.sublayers?.forEach(removeAnimations)
        if let mask = root.mask { removeAnimations(mask) }
    }
    static func animationCount(_ root: CALayer) -> Int {
        (root.animationKeys() ?? []).filter { $0.hasPrefix("telemetry.") }.count
            + (root.sublayers ?? []).reduce(0) { $0 + animationCount($1) }
            + (root.mask.map(animationCount) ?? 0)
    }
    static func bytes(_ bytes: Double?) -> String {
        guard let bytes, bytes.isFinite, bytes >= 0 else { return "—" }
        let units = ["B", "KB", "MB", "GB", "TB", "PB"]
        var value = bytes, index = 0
        while value >= 1000 && index + 1 < units.count { value /= 1000; index += 1 }
        return String(format: value >= 100 || index == 0 ? "%.0f %@" : "%.1f %@", value, units[index])
    }
    static func rate(_ value: Double?) -> String {
        guard let value, value.isFinite, value >= 0 else { return "—" }
        return bytes(value) + "/s"
    }
    static func percent(_ value: Double?) -> String {
        guard let value, value.isFinite, value >= 0 else { return "—" }
        return String(format: "%.1f%%", min(100, value))
    }
    static func time(_ date: Date) -> String {
        let formatter = DateFormatter(); formatter.locale = Locale(identifier: "en_US_POSIX")
        formatter.dateFormat = "HH:mm:ss"; return formatter.string(from: date)
    }
}

/// Fixed-size, fixed-topology vector traces. Missing samples are clipped out;
/// they never become fabricated zero readings or lines across unavailable gaps.
final class TelemetryGraph {
    let layer = CALayer()
    private struct Trace {
        let fill = CAShapeLayer(), line = CAShapeLayer()
        let fillCoverage = CAShapeLayer(), lineCoverage = CAShapeLayer()
        let marker = CALayer()
    }
    private let traces: [Trace]
    private static let count = 60
    init(name: String, frame: CGRect, colors: [NSColor]) {
        layer.name = name; layer.frame = frame; layer.masksToBounds = true
        traces = colors.map { _ in Trace() }
        let grid = CAShapeLayer(); grid.name = name + ".grid"; grid.frame = layer.bounds
        let path = CGMutablePath()
        for index in 0...6 {
            let x = frame.width * CGFloat(index) / 6
            path.move(to: CGPoint(x: x, y: 0)); path.addLine(to: CGPoint(x: x, y: frame.height))
        }
        for index in 0...2 {
            let y = frame.height * CGFloat(index) / 2
            path.move(to: CGPoint(x: 0, y: y)); path.addLine(to: CGPoint(x: frame.width, y: y))
        }
        grid.path = path; grid.fillColor = nil; grid.strokeColor = NSColor(white: 0.8, alpha: 0.14).cgColor; grid.lineWidth = 0.5
        layer.addSublayer(grid)
        for (index, trace) in traces.enumerated() {
            let color = colors[index]
            for item in [trace.fill, trace.line, trace.fillCoverage, trace.lineCoverage] { item.frame = layer.bounds }
            trace.fill.name = name + ".area.\(index)"; trace.line.name = name + ".line.\(index)"
            trace.fill.fillColor = color.withAlphaComponent(0.38).cgColor; trace.fill.strokeColor = nil
            trace.line.fillColor = nil; trace.line.strokeColor = color.cgColor; trace.line.lineWidth = 1
            trace.line.lineJoin = .round
            trace.fillCoverage.fillColor = NSColor.black.cgColor; trace.lineCoverage.fillColor = NSColor.black.cgColor
            trace.fill.mask = trace.fillCoverage; trace.line.mask = trace.lineCoverage
            layer.addSublayer(trace.fill); layer.addSublayer(trace.line)
            trace.marker.name = name + ".latest.\(index)"; trace.marker.bounds = CGRect(x: 0, y: 0, width: 3, height: 3)
            trace.marker.cornerRadius = 1.5; trace.marker.backgroundColor = color.cgColor
            layer.addSublayer(trace.marker)
        }
    }
    /// Recolor retained traces without replacing history or path animations.
    func setColors(_ colors: [NSColor]) {
        for (trace, color) in zip(traces, colors) {
            trace.fill.fillColor = color.withAlphaComponent(0.38).cgColor
            trace.line.strokeColor = color.cgColor
            trace.marker.backgroundColor = color.cgColor
        }
    }
    func update(series: [[Double?]], ceiling: Double, animated: Bool, timestamps: [TimeInterval]? = nil) {
        let limit = ceiling.isFinite && ceiling > 0 ? ceiling : 1
        for (index, trace) in traces.enumerated() {
            let raw = index < series.count ? Array(series[index].suffix(Self.count)) : []
            let values = Array<Double?>(repeating: nil, count: Self.count - raw.count) + raw.map { value in
                guard let value, value.isFinite, value >= 0 else { return nil }; return value
            }
            let height = layer.bounds.height
            let positions = samplePositions(rawCount: raw.count, timestamps: timestamps)
            let points = values.enumerated().map { index, value in
                CGPoint(x: positions[index], y: height - CGFloat(min(1, max(0, (value ?? 0) / limit))) * height)
            }
            let line = CGMutablePath(); line.move(to: points[0]); points.dropFirst().forEach { line.addLine(to: $0) }
            let area = CGMutablePath(); area.move(to: CGPoint(x: 0, y: height)); area.addLine(to: points[0])
            points.dropFirst().forEach { area.addLine(to: $0) }
            area.addLine(to: CGPoint(x: layer.bounds.width, y: height)); area.closeSubpath()
            let coverage = CGMutablePath()
            for sample in 1..<Self.count where values[sample - 1] != nil && values[sample] != nil {
                let start = positions[sample - 1], width = positions[sample] - start
                if width > 0 { coverage.addRect(CGRect(x: start, y: -1, width: width, height: height + 2)) }
            }
            trace.fillCoverage.path = coverage; trace.lineCoverage.path = coverage
            apply(area, to: trace.fill, animated: animated)
            apply(line, to: trace.line, animated: animated)
            trace.marker.isHidden = values.last! == nil
            trace.marker.position = points.last!
        }
    }
    /// Sixty vertices keep Core Animation's interpolation topology stable.
    /// Their horizontal positions reflect elapsed uptime, including reduced
    /// background cadence. Short histories retain a 59-second window; longer
    /// spans expand the window so all retained readings remain visible.
    private func samplePositions(rawCount: Int, timestamps: [TimeInterval]?) -> [CGFloat] {
        let width = layer.bounds.width
        let regular = (0..<Self.count).map { CGFloat($0) * width / CGFloat(Self.count - 1) }
        guard rawCount > 0, let timestamps, timestamps.count >= rawCount else { return regular }
        let times = Array(timestamps.suffix(rawCount))
        guard times.allSatisfy({ $0.isFinite && $0 >= 0 }),
              zip(times, times.dropFirst()).allSatisfy({ $0.1 > $0.0 }),
              let first = times.first, let latest = times.last else { return regular }
        let window = max(TimeInterval(Self.count - 1), latest - first)
        let actual = times.map { width * CGFloat(1 - (latest - $0) / window) }
        let paddingCount = Self.count - rawCount
        let padding = (0..<paddingCount).map { CGFloat($0) * actual[0] / CGFloat(paddingCount) }
        return padding + actual
    }

    private func apply(_ path: CGPath, to shape: CAShapeLayer, animated: Bool) {
        let old = shape.presentation()?.path ?? shape.path
        shape.removeAnimation(forKey: "telemetry.path")
        shape.path = path
        guard animated, let old, !CFEqual(old, path) else { return }
        let animation = CABasicAnimation(keyPath: "path"); animation.fromValue = old; animation.toValue = path
        animation.duration = 0.32; animation.timingFunction = CAMediaTimingFunction(name: .easeInEaseOut)
        shape.add(animation, forKey: "telemetry.path")
    }
}

final class StorageCanvas: NSObject, HUDModuleContentFactory {
    let layer = CALayer()
    var onChange: (() -> Void)?
    var onOpenSystemStorage: (() -> Void)?
    var animationCount: Int { TelemetryArtwork.animationCount(layer) }
    var accessibilityStatus: String {
        guard let capacity = controller.snapshot.capacity else {
            return controller.snapshot.isLoading ? L10n.text("Reading storage…", "正在读取存储…")
                : localizedError(controller.snapshot.error) ?? L10n.text("Storage unavailable", "存储不可用")
        }
        var values = [capacity.volumeName,
            L10n.text("Total ", "总计 ") + TelemetryArtwork.bytes(Double(capacity.totalBytes)),
            L10n.text("Used ", "已用 ") + TelemetryArtwork.bytes(Double(capacity.usedBytes)),
            L10n.text("Available ", "可用 ") + TelemetryArtwork.bytes(Double(capacity.availableBytes))]
        if controller.snapshot.isLoading { values.append(L10n.text("Refreshing", "正在刷新")) }
        else if let error = localizedError(controller.snapshot.error) { values.append(error) }
        return values.joined(separator: "; ")
    }
    var accessibleActions: [TelemetryCanvasAction] {
        var actions = [TelemetryCanvasAction(id: "storage:settings", label: L10n.text("Storage Settings", "存储设置"), rect: Self.settingsFrame)]
        if !controller.snapshot.isLoading {
            actions.append(TelemetryCanvasAction(id: "storage:refresh", label: L10n.text("Refresh storage", "刷新存储"), rect: Self.refreshFrame))
        }
        return actions
    }
    private static let settingsFrame = CGRect(x: 12, y: 252, width: 166, height: 29)
    private static let refreshFrame = CGRect(x: 352, y: 252, width: 36, height: 29)
    private let controller: StorageController
    private let reduceMotion: () -> Bool
    private var observer: UUID?
    private var active = false
    private var dark = true
    private var scale: CGFloat = 2
    private let heading: CATextLayer, subtitle: CATextLayer, meterCaption: CATextLayer
    private let overview = CALayer()
    private var metricRows: [(CALayer, CATextLayer, CATextLayer)] = []
    private let meterBase = CAShapeLayer(), meterUsed = CAShapeLayer()
    private let settingsButton: CALayer, settingsTitle: CATextLayer
    private let refreshButton: CALayer
    private let refreshArrow = CAShapeLayer()
    private var settingsFeedback: HUDControlHighlightLayer?
    private var refreshFeedback: HUDControlHighlightLayer?
    private var rotationGeneration = 0
    private var rotationInFlight = false
    private var queuedManualTurn = false
    private var rotationCompletion: RefreshCompletion?
    private static let rotationKey = "telemetry.storage.refresh"

    private final class RefreshCompletion: NSObject, CAAnimationDelegate {
        private let finish: (Bool) -> Void
        init(_ finish: @escaping (Bool) -> Void) { self.finish = finish }
        func animationDidStop(_ anim: CAAnimation, finished flag: Bool) { finish(flag) }
    }

    init(controller: StorageController, reduceMotion: @escaping () -> Bool = { HUDRuntimeAppearance.reduceMotion }) {
        self.controller = controller; self.reduceMotion = reduceMotion
        layer.name = "module.storage.canvas"; layer.frame = CGRect(x: 0, y: 0, width: 400, height: 334)
        heading = TelemetryArtwork.text("storage.heading", frame: CGRect(x: 12, y: 0, width: 376, height: 20), size: 15, parent: layer, weight: .semibold)
        subtitle = TelemetryArtwork.text("storage.caption", frame: CGRect(x: 12, y: 24, width: 376, height: 13), size: 9, parent: layer)
        overview.frame = layer.bounds; layer.addSublayer(overview)
        meterCaption = TelemetryArtwork.text("storage.meterCaption", frame: CGRect(x: 12, y: 224, width: 376, height: 15), size: 10, parent: overview)
        settingsButton = TelemetryArtwork.plate(Self.settingsFrame, parent: layer, name: "storage.settings.button")
        settingsTitle = TelemetryArtwork.text("storage.settings.title", frame: CGRect(x: 5, y: 6, width: 156, height: 17), size: 11, parent: settingsButton, weight: .semibold, alignment: .center)
        refreshButton = TelemetryArtwork.plate(Self.refreshFrame, parent: layer, name: "storage.refresh.button")
        super.init()
        for index in 0..<3 {
            let plate = TelemetryArtwork.plate(CGRect(x: 12, y: 49 + CGFloat(index) * 47, width: 376, height: 40), parent: overview, name: "storage.metric.\(index)")
            let title = TelemetryArtwork.text("storage.metricTitle.\(index)", frame: CGRect(x: 10, y: 13, width: 116, height: 15), size: 11, parent: plate, weight: .medium)
            let value = TelemetryArtwork.text("storage.metricValue.\(index)", frame: CGRect(x: 133, y: 9, width: 231, height: 25), size: 18, parent: plate, weight: .medium, alignment: .right)
            metricRows.append((plate, title, value))
        }
        for shape in [meterBase, meterUsed] { shape.frame = CGRect(x: 12, y: 201, width: 376, height: 17); overview.addSublayer(shape) }
        meterBase.name = "storage.capacity.segments"; meterUsed.name = "storage.capacity.used"
        refreshArrow.name = "storage.refresh.arrow"
        refreshArrow.bounds = CGRect(x: 0, y: 0, width: 22, height: 22)
        refreshArrow.position = CGPoint(x: refreshButton.bounds.midX, y: refreshButton.bounds.midY)
        refreshArrow.anchorPoint = CGPoint(x: 0.5, y: 0.5)
        refreshArrow.strokeColor = nil; refreshArrow.lineWidth = 0
        // A filled, clockwise arrow avoids the doubled V-shaped tip of the
        // former stroke. Its full ink bounds and the circular shaft share the
        // same center, so a turn cannot look like an off-center orbit.
        let shaft = CGMutablePath(), center = CGPoint(x: 11, y: 11), radius: CGFloat = 6.2
        shaft.addArc(center: center, radius: radius, startAngle: .pi / 3,
                     endAngle: .pi * 65 / 36, clockwise: false)
        let path = CGMutablePath()
        path.addPath(shaft.copy(strokingWithWidth: 1.7, lineCap: .round,
                               lineJoin: .round, miterLimit: 1))
        path.move(to: CGPoint(x: 18.05, y: 10.1))
        path.addLine(to: CGPoint(x: 13.25, y: 10.1))
        path.addLine(to: CGPoint(x: 18.05, y: 5.3)); path.closeSubpath()
        refreshArrow.path = path; refreshButton.addSublayer(refreshArrow)
        settingsFeedback = HUDControlHighlightLayer.add(to: settingsButton, rect: settingsButton.bounds)
        refreshFeedback = HUDControlHighlightLayer.add(to: refreshButton, rect: refreshButton.bounds)
        render()
    }
    deinit {
        if let observer { controller.removeObserver(observer) }
        if active { controller.deactivate() }
        cancelRefreshAnimation()
        TelemetryArtwork.removeAnimations(layer)
    }
    func makeContent(for module: HUDModule, style: HUDModuleContentStyle) -> CALayer {
        dark = style.dark; scale = style.contentsScale; render(); return layer
    }
    func activate() {
        guard !active else { return }
        active = true
        observer = controller.observe { [weak self] in
            guard let self, self.active else { return }; self.render(); self.onChange?()
        }
        controller.activate(); render(); onChange?()
    }
    func deactivate() {
        active = false
        if let observer { controller.removeObserver(observer) }; observer = nil
        controller.deactivate(); cancelRefreshAnimation(); TelemetryArtwork.removeAnimations(layer)
    }
    func updateRenderScale(_ value: CGFloat) {
        scale = value.isFinite ? min(8, max(1, value)) : 2
        TelemetryArtwork.withoutActions { TelemetryArtwork.scale(layer, scale) }
        updateRefreshAnimation()
    }
    @discardableResult func mouseDown(at point: CGPoint) -> Bool {
        guard active, point.x.isFinite, point.y.isFinite, layer.bounds.contains(point) else { return false }
        if let action = accessibleActions.first(where: { $0.rect.contains(point) }) { perform(actionID: action.id) }
        return true
    }
    func perform(actionID: String) {
        guard active, accessibleActions.contains(where: { $0.id == actionID }) else { return }
        switch actionID {
        case "storage:settings": onOpenSystemStorage?()
        case "storage:refresh":
            // Submit the visual turn before a synchronous or very fast query
            // can finish. Repeated clicks add at most one coalesced extra turn.
            if !reduceMotion() {
                if rotationInFlight { queuedManualTurn = true }
                else { beginRefreshTurn() }
            }
            controller.refresh()
        default: break
        }
    }
    func cancelDetail() -> Bool { false }
    @discardableResult func scroll(at point: CGPoint, delta: CGFloat) -> Bool { false }

    private func render() {
        TelemetryArtwork.withoutActions {
            heading.string = HUDModule.storage.title; heading.foregroundColor = TelemetryArtwork.primary(dark).cgColor
            // The volume label stays quiet during routine refreshes; only a
            // genuine query failure replaces it with an actionable status.
            subtitle.string = !controller.snapshot.isLoading ? localizedError(controller.snapshot.error)
                ?? controller.snapshot.capacity?.volumeName ?? L10n.text("Startup disk", "启动磁盘")
                : controller.snapshot.capacity?.volumeName ?? L10n.text("Startup disk", "启动磁盘")
            subtitle.foregroundColor = TelemetryArtwork.muted(dark).cgColor
            let titles = [L10n.text("Total", "总计"), L10n.text("Used", "已用"), L10n.text("Available", "可用")]
            let capacity = controller.snapshot.capacity
            let values = [capacity.map { Double($0.totalBytes) }, capacity.map { Double($0.usedBytes) }, capacity.map { Double($0.availableBytes) }]
            for (index, row) in metricRows.enumerated() {
                TelemetryArtwork.stylePlate(row.0, dark: dark); row.1.string = titles[index]; row.1.foregroundColor = TelemetryArtwork.rowText.cgColor
                row.2.string = TelemetryArtwork.bytes(values[index]); row.2.foregroundColor = (index == 0 ? TelemetryArtwork.rowText : (index == 1 ? TelemetryArtwork.yellow : TelemetryArtwork.cyan)).cgColor
            }
            let fraction = capacity.map { Double($0.usedBytes) / Double($0.totalBytes) }
            meterBase.path = segmentedMeter(fraction: 1); meterBase.fillColor = TelemetryArtwork.muted(dark).withAlphaComponent(0.22).cgColor
            meterUsed.path = segmentedMeter(fraction: fraction ?? 0); meterUsed.fillColor = TelemetryArtwork.yellow.cgColor
            meterUsed.isHidden = fraction == nil
            meterCaption.string = fraction.map { TelemetryArtwork.percent($0 * 100) + L10n.text(" used", " 已用") } ?? L10n.text("Capacity unavailable", "容量不可用")
            meterCaption.foregroundColor = TelemetryArtwork.muted(dark).cgColor
            settingsButton.backgroundColor = TelemetryArtwork.yellow.cgColor
            settingsTitle.string = L10n.text("Storage Settings", "存储设置"); settingsTitle.foregroundColor = NSColor(white: 0.13, alpha: 1).cgColor
            refreshButton.backgroundColor = NSColor(white: dark ? 0.81 : 0.90, alpha: 1).cgColor
            refreshArrow.fillColor = NSColor(white: 0.13, alpha: 1).cgColor
            refreshButton.opacity = 1
            refreshFeedback?.setEnabled(!controller.snapshot.isLoading)
            TelemetryArtwork.scale(layer, scale)
        }
        updateRefreshAnimation()
    }
    private func updateRefreshAnimation() {
        guard active, !reduceMotion() else { cancelRefreshAnimation(); return }
        // Finishing a read must not remove the current rotation halfway round.
        // Its completion either starts the next turn or rests at the identical
        // 2π/zero orientation. No timer or repeat-forever animation is needed.
        if controller.snapshot.isLoading && !rotationInFlight { beginRefreshTurn() }
    }

    private func beginRefreshTurn() {
        guard active, !reduceMotion(), !rotationInFlight else { return }
        rotationGeneration += 1; let token = rotationGeneration
        rotationInFlight = true
        let completion = RefreshCompletion { [weak self] finished in
            guard let self, token == self.rotationGeneration else { return }
            self.rotationInFlight = false; self.rotationCompletion = nil
            // Invalidating the token before removal also rejects any duplicate
            // or interrupted callback from Core Animation's copied animation.
            self.rotationGeneration += 1
            self.refreshArrow.removeAnimation(forKey: Self.rotationKey)
            guard finished, self.active, !self.reduceMotion() else {
                self.queuedManualTurn = false; return
            }
            let continueTurning = self.controller.snapshot.isLoading || self.queuedManualTurn
            self.queuedManualTurn = false
            if continueTurning { self.beginRefreshTurn() }
        }
        rotationCompletion = completion
        let animation = CABasicAnimation(keyPath: "transform.rotation.z")
        animation.fromValue = 0; animation.toValue = CGFloat.pi * 2
        animation.duration = 0.72; animation.repeatCount = 0
        animation.timingFunction = CAMediaTimingFunction(name: .linear)
        animation.delegate = completion
        refreshArrow.add(animation, forKey: Self.rotationKey)
    }

    private func cancelRefreshAnimation() {
        rotationGeneration += 1; rotationInFlight = false; queuedManualTurn = false
        rotationCompletion = nil
        refreshArrow.removeAnimation(forKey: Self.rotationKey)
        TelemetryArtwork.withoutActions { refreshArrow.transform = CATransform3DIdentity }
    }

    private func segmentedMeter(fraction: Double) -> CGPath {
        let path = CGMutablePath(), count = 32, width = (meterBase.bounds.width - 3 * CGFloat(count - 1)) / CGFloat(count)
        let filled = min(1, max(0, fraction)) * Double(count)
        for index in 0..<count {
            let amount = min(1, max(0, filled - Double(index)))
            if amount > 0 { path.addRect(CGRect(x: CGFloat(index) * (width + 3), y: 0, width: width * CGFloat(amount), height: meterBase.bounds.height)) }
        }
        return path
    }
    private func localizedError(_ value: String?) -> String? {
        switch value {
        case "The startup filesystem is unavailable.": return L10n.text("The startup filesystem is unavailable.", "启动文件系统不可用。")
        case "Storage capacity is unavailable.": return L10n.text("Storage capacity is unavailable.", "无法读取存储容量。")
        default: return value
        }
    }
}
