import AppKit
import QuartzCore

// Build-only oracle for the Windows Power area. The unchanged ChargeIndicatorView,
// HUDChargeBadge, HUDChargeMetric, DisplayPolicy, OverlayGeometry,
// DeviceBatteryProvider, HUDDeploymentFlicker and battery model sources are
// compiled with the extracted ChargeReferenceFixture.swift. Their Core
// Animation clock is frozen (speed 0) inside a never-ordered window, so every
// presentation sample is deterministic. No HUD, controller, power source,
// account or user data is touched.
enum HUDRuntimeAppearance { static var reduceMotion = false }
enum L10n {
    static var useChinese = false
    static func text(_ english: String, _ simplifiedChinese: String) -> String { useChinese ? simplifiedChinese : english }
}
struct SystemActivitySnapshot: Equatable {
    var cpuPercent: Double? = nil
    var memoryUsedBytes: UInt64? = nil
    var memoryTotalBytes: UInt64? = nil
    var uploadBytesPerSecond: Double? = nil
    var downloadBytesPerSecond: Double? = nil
    var diskReadBytesPerSecond: Double? = nil
    var diskWriteBytesPerSecond: Double? = nil
}

let sRGB = CGColorSpace(name: CGColorSpace.sRGB)!
func rgba(_ color: CGColor?) -> Any {
    guard let color, let converted = color.converted(to: sRGB, intent: .defaultIntent, options: nil),
          let components = converted.components else { return NSNull() }
    return components.map { Double($0) }
}
func optional<T>(_ value: T?) -> Any { value.map { $0 as Any } ?? NSNull() }
// JSON has no non-finite numbers; the native oracle reads these as strings.
func jsonSafe(_ value: Any) -> Any {
    if let number = value as? Double, !number.isFinite { return number.isNaN ? "nan" : (number > 0 ? "inf" : "-inf") }
    if let number = value as? CGFloat, !number.isFinite { return number.isNaN ? "nan" : (number > 0 ? "inf" : "-inf") }
    if let array = value as? [Any] { return array.map(jsonSafe) }
    if let object = value as? [String: Any] { return object.mapValues(jsonSafe) }
    return value
}
func rect(_ r: CGRect) -> [Double] { [r.origin.x, r.origin.y, r.width, r.height].map { Double($0) } }
func describe(_ s: BatterySnapshot) -> [String: Any] {
    ["percentage": optional(s.percentage), "pluggedIn": s.isPluggedIn, "charging": s.isCharging,
     "fullyCharged": s.isFullyCharged, "hasBattery": s.hasBattery,
     "capacity": s.capacity.map { ["current": $0.current, "maximum": $0.maximum, "unit": $0.unit.rawValue] as Any } ?? NSNull(),
     "health": optional(s.healthCategory)]
}
func describe(_ t: SystemActivitySnapshot?) -> Any {
    guard let t else { return NSNull() }
    return ["cpuPercent": optional(t.cpuPercent), "memoryUsed": optional(t.memoryUsedBytes), "memoryTotal": optional(t.memoryTotalBytes),
            "upload": optional(t.uploadBytesPerSecond), "download": optional(t.downloadBytesPerSecond),
            "diskRead": optional(t.diskReadBytesPerSecond), "diskWrite": optional(t.diskWriteBytesPerSecond)]
}

final class Frozen {
    let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 1000, height: 640), styleMask: [.borderless],
                          backing: .buffered, defer: false)
    let host = CALayer()
    init() {
        let view = NSView(frame: NSRect(x: 0, y: 0, width: 1000, height: 640)); view.wantsLayer = true
        window.contentView = view
        host.frame = view.bounds; host.speed = 0; host.timeOffset = 0; host.isGeometryFlipped = true
        view.layer!.addSublayer(host); CATransaction.flush()
    }
    func at(_ time: Double) {
        CATransaction.begin(); CATransaction.setDisableActions(true); host.timeOffset = time; CATransaction.commit(); CATransaction.flush()
    }
}
func spin(until deadline: Date) {
    while Date() < deadline { RunLoop.main.run(until: min(deadline, Date().addingTimeInterval(0.005))) }
    RunLoop.main.run(until: Date().addingTimeInterval(0.002))
}

func presentation(_ layer: CALayer) -> CALayer { layer.presentation() ?? layer }
func pathPoints(_ path: CGPath?) -> [[Double]] {
    var points: [[Double]] = []
    path?.applyWithBlock { element in
        let count: Int
        switch element.pointee.type {
        case .moveToPoint, .addLineToPoint: count = 1
        case .addQuadCurveToPoint: count = 2
        case .addCurveToPoint: count = 3
        default: count = 0
        }
        for index in 0..<count { points.append([Double(element.pointee.points[index].x), Double(element.pointee.points[index].y)]) }
    }
    return points
}
func pathOps(_ path: CGPath?) -> [String] {
    var ops: [String] = []
    path?.applyWithBlock { element in
        switch element.pointee.type {
        case .moveToPoint: ops.append("move")
        case .addLineToPoint: ops.append("line")
        case .addQuadCurveToPoint: ops.append("quad")
        case .addCurveToPoint: ops.append("cubic")
        case .closeSubpath: ops.append("close")
        @unknown default: ops.append("?")
        }
    }
    return ops
}
func sample(_ view: ChargeIndicatorView, time: Double) -> [String: Any] {
    let canvas = view.embeddedContentLayer, layers = canvas.sublayers!
    let shadow = presentation(layers[0]), body = presentation(layers[1])
    let emblem = presentation(layers[2]) as! CAShapeLayer, bolt = presentation(layers[3]) as! CAShapeLayer
    let ring = presentation(layers[8])
    func surface(_ l: CALayer) -> [Double] {
        [l.bounds.width, l.bounds.height, l.cornerRadius, l.transform.m11, l.transform.m22, Double(l.opacity),
         l.borderWidth, l.position.x, l.position.y].map { Double($0) }
    }
    return ["t": time, "stage": view.stage.rawValue,
            "body": surface(body), "shadow": surface(shadow),
            "bodyBorder": rgba(body.borderColor), "bodyFill": rgba(body.backgroundColor),
            "emblem": [emblem.bounds.width, emblem.bounds.height, emblem.position.x, emblem.position.y,
                       emblem.transform.m11, CGFloat(emblem.opacity)].map { Double($0) },
            "emblemPath": pathPoints(emblem.path), "emblemOps": pathOps(emblem.path), "emblemFill": rgba(emblem.fillColor),
            "bolt": [bolt.position.x, bolt.position.y, bolt.transform.m11, CGFloat(bolt.opacity)].map { Double($0) },
            "boltFill": rgba(bolt.fillColor),
            "texts": (4...7).map { index -> [Double] in
                let l = presentation(layers[index])
                return [l.position.x, l.position.y, CGFloat(l.opacity), l.bounds.width, l.bounds.height].map { Double($0) } },
            "ring": [ring.position.x, ring.position.y, CGFloat(ring.opacity)].map { Double($0) },
            "ripples": layers[1].sublayers!.map { raw -> [Double] in
                let l = presentation(raw)
                return [l.position.x, l.position.y, l.transform.m11, CGFloat(l.opacity)].map { Double($0) } },
            "canvasOpacity": Double(presentation(canvas).opacity),
            "embeddedBodyRect": rect(view.embeddedBodyRect)]
}

let demo = BatterySnapshot(percentage: 75, isPluggedIn: true, isCharging: true, isFullyCharged: false, hasBattery: true,
                           capacity: BatteryCapacityReading(current: 3600, maximum: 4800, unit: .milliampHours))
let snapshots: [(String, BatterySnapshot)] = [
    ("demoCharging", demo),
    ("connected49", BatterySnapshot(percentage: 49, isPluggedIn: true, isCharging: false, isFullyCharged: false, hasBattery: true,
                                    capacity: BatteryCapacityReading.fromRegistry([:], percentage: 49))),
    ("battery19", BatterySnapshot(percentage: 19, isPluggedIn: false, isCharging: false, isFullyCharged: false, hasBattery: true,
                                  capacity: BatteryCapacityReading.fromRegistry([:], percentage: 19))),
    ("battery20", BatterySnapshot(percentage: 20, isPluggedIn: false, isCharging: false, isFullyCharged: false, hasBattery: true,
                                  capacity: BatteryCapacityReading(current: 12345, maximum: 1234567, unit: .milliampHours))),
    ("full100", BatterySnapshot(percentage: 100, isPluggedIn: true, isCharging: false, isFullyCharged: true, hasBattery: true,
                                capacity: BatteryCapacityReading.fromRegistry([:], percentage: 100))),
    ("unknownPercent", BatterySnapshot(percentage: nil, isPluggedIn: false, isCharging: false, isFullyCharged: false, hasBattery: true)),
    ("unavailable", .unavailable),
]
let telemetryFull = SystemActivitySnapshot(cpuPercent: 37.25, memoryUsedBytes: 9_663_676_416, memoryTotalBytes: 17_179_869_184,
                                           uploadBytesPerSecond: 1_234.5, downloadBytesPerSecond: 987_654_321,
                                           diskReadBytesPerSecond: 12_000, diskWriteBytesPerSecond: 999.94)
let telemetries: [(String, SystemActivitySnapshot?)] = [
    ("none", nil),
    ("full", telemetryFull),
    ("partial", SystemActivitySnapshot(cpuPercent: 100, memoryUsedBytes: 20, memoryTotalBytes: 10,
                                       uploadBytesPerSecond: nil, downloadBytesPerSecond: 0,
                                       diskReadBytesPerSecond: -1, diskWriteBytesPerSecond: .infinity)),
    ("large", SystemActivitySnapshot(cpuPercent: 101, memoryUsedBytes: 0, memoryTotalBytes: 1_073_741_824 * 12_000,
                                     uploadBytesPerSecond: 2.5e15, downloadBytesPerSecond: 9.99e14,
                                     diskReadBytesPerSecond: 999.95, diskWriteBytesPerSecond: 1_000)),
    ("nan", SystemActivitySnapshot(cpuPercent: .nan, memoryUsedBytes: 5, memoryTotalBytes: 0,
                                   uploadBytesPerSecond: .nan, downloadBytesPerSecond: 10_000.04,
                                   diskReadBytesPerSecond: 99_999.5, diskWriteBytesPerSecond: 0.05)),
]

@main enum Main {
    static func main() throws {
        precondition(CommandLine.arguments.count == 2 && ProcessInfo.processInfo.environment["CFFIXED_USER_HOME"] != nil)
        NSApplication.shared.setActivationPolicy(.prohibited)
        let out = URL(fileURLWithPath: CommandLine.arguments[1])
        let encoder = try ModuleReferenceLayerEncoder(output: out)
        var result: [String: Any] = [:]

        // A. Settled model trees per stage, theme, language, reading and metric.
        var trees: [[String: Any]] = []
        func tree(_ name: String, snapshot: BatterySnapshot, dark: Bool, chinese: Bool, metric: HUDChargeMetric,
                  telemetry: (String, SystemActivitySnapshot?), stage: OverlayStage, hovered: Bool, preview: Bool,
                  accentHex: String = "FAD41F") throws {
            L10n.useChinese = chinese
            let view = ChargeIndicatorView(frame: NSRect(origin: .zero, size: ChargeIndicatorView.canvasSize))
            var configuration = AppConfiguration.defaults
            configuration.alertMetric = metric; configuration.accentHex = accentHex
            view.configureEmbedding(dark: dark, contentsScale: 2)
            view.set(snapshot: snapshot, configuration: configuration, preview: preview)
            view.setMetric(metric, telemetry: telemetry.1)
            view.setStage(stage)
            if hovered { view.setEmbeddedHovered(true, animated: false) }
            CATransaction.flush()
            trees.append(["snapshot": name, "battery": describe(snapshot), "dark": dark, "chinese": chinese,
                          "metric": metric.rawValue, "telemetry": telemetry.0, "telemetryValue": describe(telemetry.1),
                          "stage": stage.rawValue, "hovered": hovered, "preview": preview, "accentHex": accentHex,
                          "accessibility": view.accessibilityLabel() ?? "", "embeddedBodyRect": rect(view.embeddedBodyRect),
                          "layer": try encoder.encode(view.embeddedContentLayer, id: "charge")])
        }
        for dark in [true, false] { for chinese in [false, true] { for (name, snapshot) in snapshots { for stage in OverlayStage.allCases {
            try tree(name, snapshot: snapshot, dark: dark, chinese: chinese, metric: .battery, telemetry: ("none", nil),
                     stage: stage, hovered: false, preview: false)
        }}}}
        for metric in HUDChargeMetric.allCases where metric != .battery { for telemetry in telemetries { for chinese in [false, true] {
            try tree("demoCharging", snapshot: demo, dark: true, chinese: chinese, metric: metric, telemetry: telemetry,
                     stage: .compact, hovered: false, preview: false)
        }}}
        for dark in [true, false] { for stage in [OverlayStage.compact, .circle] {
            try tree("demoCharging", snapshot: demo, dark: dark, chinese: false, metric: .battery, telemetry: ("none", nil),
                     stage: stage, hovered: true, preview: false, accentHex: "3FA9F5")
        }}
        for chinese in [false, true] {
            try tree("battery19", snapshot: snapshots[2].1, dark: true, chinese: chinese, metric: .battery, telemetry: ("none", nil),
                     stage: .compact, hovered: false, preview: true)
        }
        L10n.useChinese = false
        result["trees"] = trees
        result["unsupported"] = encoder.unsupported

        // B. Frozen-clock presentation timelines of the unchanged finite sequences.
        var timelines: [[String: Any]] = []
        let frozen = Frozen()
        func indicator() -> ChargeIndicatorView {
            let view = ChargeIndicatorView(frame: NSRect(origin: .zero, size: ChargeIndicatorView.canvasSize))
            view.configureEmbedding(dark: true, contentsScale: 2)
            view.set(snapshot: demo, configuration: .defaults)
            frozen.host.sublayers?.forEach { $0.removeFromSuperlayer() }
            frozen.host.addSublayer(view.embeddedContentLayer)
            frozen.at(0)
            return view
        }
        struct Event { let at: Double; let action: () -> Void }
        func timeline(_ name: String, _ view: ChargeIndicatorView, _ events: [Event], end: Double, step: Double = 1.0 / 120) {
            var samples: [[String: Any]] = []
            for (index, event) in events.enumerated() {
                frozen.at(event.at); event.action(); CATransaction.flush()
                let limit = index + 1 < events.count ? events[index + 1].at : end + step / 2
                var t = event.at
                while t < limit - 1e-9 { frozen.at(t); samples.append(sample(view, time: t)); t += step }
            }
            timelines.append(["name": name, "samples": samples])
        }
        do {
            let view = indicator(); view.setStage(.hidden)
            var start = Date()
            timeline("entrance", view, [
                Event(at: 0) { start = Date(); view.animateEntrance() },
                Event(at: 0.20) { precondition(Date() < start.addingTimeInterval(1.0)); spin(until: start.addingTimeInterval(0.23)) },
                Event(at: 1.22) { spin(until: start.addingTimeInterval(1.25)) },
                Event(at: 1.50) { spin(until: start.addingTimeInterval(1.53)) },
            ], end: 1.75)
        }
        do {
            let view = indicator(); view.setStage(.compact)
            var start = Date()
            timeline("exit", view, [
                Event(at: 0) { start = Date(); view.animateExit() },
                Event(at: 0.30) { spin(until: start.addingTimeInterval(0.33)) },
            ], end: 0.72)
        }
        do {
            let view = indicator(); view.setStage(.hidden)
            var start = Date(), exitStart = Date()
            timeline("interruptedEntrance", view, [
                Event(at: 0) { start = Date(); view.animateEntrance() },
                Event(at: 0.20) { spin(until: start.addingTimeInterval(0.23)) },
                Event(at: 0.55) { exitStart = Date(); view.animateExit() },
                Event(at: 0.85) { spin(until: max(exitStart.addingTimeInterval(0.33), start.addingTimeInterval(1.6))) },
            ], end: 1.30)
        }
        do {
            let view = indicator(); view.setStage(.compact)
            timeline("setStageAnimated", view, [
                Event(at: 0) { view.setStage(.circle, animated: true) },
                Event(at: 0.10) { view.setStage(.supercharge, animated: true) },
                Event(at: 0.40) { view.setStage(.compact, animated: true) },
            ], end: 0.75)
        }
        do {
            let view = indicator(); view.setStage(.circle)
            timeline("morphReversal", view, [
                Event(at: 0) { view.morphEmbeddedStage(.compact, animated: true) },
                Event(at: 0.10) { view.morphEmbeddedStage(.circle, animated: true) },
                Event(at: 0.45) { view.morphEmbeddedStage(.compact, animated: true) },
            ], end: 0.80)
        }
        do {
            let view = indicator(); view.setStage(.compact)
            timeline("hover", view, [
                Event(at: 0) { view.setEmbeddedHovered(true, animated: true) },
                Event(at: 0.08) { view.setEmbeddedHovered(false, animated: true) },
                Event(at: 0.30) { view.setEmbeddedHovered(true, animated: true) },
            ], end: 0.55)
        }
        do {
            let view = indicator(); view.setStage(.circle)
            timeline("hiddenFromCircle", view, [
                Event(at: 0) { view.setStage(.hidden, animated: true) },
            ], end: 0.35)
        }
        do {
            HUDRuntimeAppearance.reduceMotion = true
            let view = indicator(); view.setStage(.hidden)
            timeline("reducedEntrance", view, [
                Event(at: 0) { view.animateEntrance() },
                Event(at: 0.05) { view.setEmbeddedHovered(true, animated: true) },
                Event(at: 0.10) { view.animateExit() },
            ], end: 0.15, step: 0.025)
            HUDRuntimeAppearance.reduceMotion = false
        }
        result["timelines"] = timelines

        // C. Pure metric projections, including invalid/partial telemetry.
        var readings: [[String: Any]] = []
        for chinese in [false, true] {
            L10n.useChinese = chinese
            for metric in HUDChargeMetric.allCases { for (name, snapshot) in snapshots { for telemetry in telemetries {
                if metric == .battery && telemetry.0 != "none" { continue }
                if metric != .battery && name != "demoCharging" && name != "unavailable" { continue }
                let r = HUDChargeMetricReading.resolve(metric: metric, battery: snapshot, telemetry: telemetry.1)
                readings.append(["metric": metric.rawValue, "title": metric.title, "chinese": chinese, "snapshot": name,
                                 "battery": describe(snapshot), "telemetry": describe(telemetry.1),
                                 "primary": r.primary, "secondary": r.secondary, "unit": r.unit, "trailing": r.trailing,
                                 "progress": optional(r.progress), "accessibility": r.accessibilityValue])
            }}}
        }
        L10n.useChinese = false
        result["readings"] = readings

        // D. HUDChargeBadge phases with an injected scheduler and frozen renderer clock.
        do {
            var pending: [(delay: Double, action: () -> Void, cancelled: Bool)] = []
            var badgeRows: [[String: Any]] = []
            let badge = HUDChargeBadge(scheduler: { delay, action in
                pending.append((delay, action, false)); let index = pending.count - 1
                return { pending[index].cancelled = true }
            })
            frozen.host.sublayers?.forEach { $0.removeFromSuperlayer() }
            frozen.host.addSublayer(badge.layer)
            badge.update(snapshot: demo, configuration: .defaults, dark: true, contentsScale: 2)
            let center = HUDChargeBadge.center
            var probes: [CGPoint] = []
            for y in stride(from: 404.0, through: 488.0, by: 6.0) { for x in stride(from: 360.0, through: 640.0, by: 8.0) {
                probes.append(CGPoint(x: x, y: y)) } }
            for dx in [-109.5, -108.5, -16.5, -15.0, 0, 15.0, 16.5, 108.5, 109.5, 111.5, 112.5] { for dy in [-17.0, -15.5, -14.0, 0, 14.0, 15.5, 17.0, 18.0] {
                probes.append(CGPoint(x: center.x + dx, y: center.y + dy)) } }
            func record(_ label: String, _ time: Double) {
                frozen.at(time)
                badgeRows.append(["label": label, "t": time, "stage": badge.stage.rawValue, "hovered": badge.isHovered,
                                  "hitRect": rect(badge.hitRect), "accessibility": badge.accessibilityLabel,
                                  "contains": String(probes.map { badge.contains($0) ? "1" : "0" }.joined()),
                                  "pending": pending.filter { !$0.cancelled }.map { $0.delay }])
            }
            func fire(_ index: Int) { let item = pending[index]; precondition(!item.cancelled); pending[index].cancelled = true; item.action() }
            frozen.at(0); badge.animateEntrance(); CATransaction.flush(); record("waiting", 0); record("waiting", 0.3)
            frozen.at(0.58); var start = Date(); fire(0); CATransaction.flush(); record("entering", 0.58)
            for t in [0.62, 0.70, 0.76] { record("entering", t) }
            frozen.at(0.78); spin(until: start.addingTimeInterval(0.23)); for t in [0.78, 0.9, 1.08, 1.3, 1.79] { record("entering", t) }
            frozen.at(1.80); spin(until: start.addingTimeInterval(1.25)); for t in [1.80, 1.9, 2.0, 2.07] { record("entering", t) }
            frozen.at(2.08); spin(until: start.addingTimeInterval(1.53)); record("presented", 2.08); record("presented", 4.9)
            frozen.at(5.08); fire(pending.count - 1); CATransaction.flush()
            for t in [5.08, 5.14, 5.21, 5.30, 5.4] { record("collapsing", t) }
            frozen.at(5.5); badge.setHovered(true); CATransaction.flush(); for t in [5.5, 5.58, 5.66, 5.8] { record("hover", t) }
            frozen.at(5.9); badge.setHovered(false); CATransaction.flush(); for t in [5.9, 5.98, 6.3] { record("unhover", t) }
            frozen.at(6.4); badge.setHovered(true); CATransaction.flush(); record("hover", 6.45)
            frozen.at(6.5); start = Date(); badge.animateExit(); CATransaction.flush(); for t in [6.5, 6.6, 6.79] { record("exiting", t) }
            frozen.at(6.8); spin(until: start.addingTimeInterval(0.33)); for t in [6.8, 7.0, 7.2] { record("exiting", t) }
            // Close before the delayed reveal; then a stable reopen.
            frozen.at(8.0); badge.animateEntrance(); CATransaction.flush(); record("waiting", 8.0)
            frozen.at(8.2); badge.animateExit(); CATransaction.flush(); record("closedBeforeReveal", 8.2)
            frozen.at(9.0); badge.setStable(); CATransaction.flush(); record("stable", 9.0)
            frozen.at(9.1); badge.setHovered(true); CATransaction.flush(); record("stableHover", 9.1)
            frozen.at(9.2); badge.setStable(visible: false); CATransaction.flush(); record("stableHidden", 9.2)
            HUDRuntimeAppearance.reduceMotion = true
            frozen.at(10); badge.animateEntrance(); CATransaction.flush(); record("reducedEntrance", 10)
            frozen.at(10.1); badge.animateExit(); CATransaction.flush(); record("reducedExit", 10.1)
            HUDRuntimeAppearance.reduceMotion = false
            result["badge"] = ["rows": badgeRows, "probes": probes.map { [Double($0.x), Double($0.y)] },
                               "frame": rect(HUDChargeBadge.frame), "compactHitRect": rect(HUDChargeBadge.compactHitRect),
                               "circleHitRect": rect(HUDChargeBadge.circleHitRect), "center": [Double(center.x), Double(center.y)],
                               "rendererScale": Double(HUDChargeBadge.rendererScale), "entranceDelay": HUDChargeBadge.entranceDelay,
                               "compactHoldDuration": HUDChargeBadge.compactHoldDuration,
                               "entranceDuration": HUDChargeBadge.entranceDuration, "exitDuration": HUDChargeBadge.exitDuration]
        }

        // E. Seeded deployment flicker sequences (the badge itself draws a fresh seed).
        var flicker: [[String: Any]] = []
        for seed: UInt64 in [0, 1, 42, 0x9E3779B97F4A7C15, UInt64.max] { for opening in [true, false] {
            for duration: TimeInterval? in [nil, 0.18, 0.11, 0.6, 0.05, .nan] { for delay in [0, 0.14, 0.07, -1] {
                let s = HUDDeploymentFlicker.sequence(opening: opening, duration: duration, delay: delay, seed: seed)
                flicker.append(["seed": String(seed), "opening": opening, "duration": duration.map { $0.isNaN ? "nan" : String($0) } ?? "nil",
                                "delay": delay, "keyTimes": s.keyTimes, "offsets": s.opacityOffsets,
                                "sequenceDuration": s.duration, "sequenceDelay": s.delay])
            }}
        }}
        result["flicker"] = flicker

        // F. DisplayPolicy over baselines, transitions and both modes.
        var policy: [[String: Any]] = []
        let previousCases: [(String, BatterySnapshot?)] = [("nil", nil)] + snapshots.map { ($0.0, Optional($0.1)) }
        for mode in DisplayMode.allCases { for (name, snapshot) in snapshots { for (previousName, previous) in previousCases {
            let action = DisplayPolicy.action(for: snapshot, previous: previous, mode: mode)
            let label: String
            switch action { case .hide: label = "hide"; case .showPersistent: label = "showPersistent"
            case .showTransient: label = "showTransient"; case .keepCurrent: label = "keepCurrent" }
            policy.append(["mode": mode.rawValue, "snapshot": name, "previous": previousName, "action": label])
        }}}
        result["policy"] = policy

        // G. Overlay geometry in source (bottom-left origin) desktop points.
        var geometry: [[String: Any]] = []
        let screens = [NSRect(x: 0, y: 0, width: 1440, height: 900), NSRect(x: 0, y: 25, width: 1440, height: 850),
                       NSRect(x: -1920, y: -200, width: 1920, height: 1055), NSRect(x: 0, y: 0, width: 200, height: 60)]
        let points = [NSPoint(x: 720, y: 450), NSPoint(x: 0, y: 0), NSPoint(x: 5000, y: -5000), NSPoint(x: CGFloat.nan, y: 100),
                      NSPoint(x: 100, y: CGFloat.infinity), NSPoint(x: -1900, y: 840)]
        for screen in screens { for point in points { for scale in [0.5, 0.65, 1, 1.25, 1.6, 2, Double.nan] { for editing in [false, true] {
            let a = OverlayGeometry.clampedAnchor(point, screen: screen, scale: scale, editing: editing)
            geometry.append(["kind": "clamp", "screen": rect(screen), "point": [Double(point.x), Double(point.y)].map { $0.isFinite ? $0 as Any : String($0) },
                             "scale": scale.isNaN ? "nan" as Any : scale, "editing": editing, "anchor": [Double(a.x), Double(a.y)]])
        }}}}
        for screen in screens { for placement in OverlayPlacement.allCases { for position in [(0.5, 0.9), (0, 0), (1, 1), (0.25, 1.4), (Double.nan, -2)] {
            for scale in [1.0, 1.6, 0.3] { for editing in [false, true] {
                var configuration = AppConfiguration.defaults
                configuration.placement = placement; configuration.scale = scale
                configuration.customPosition = OverlayPosition(screenID: 7, x: position.0, y: position.1)
                let a = OverlayGeometry.anchor(configuration: configuration, screen: screen, editing: editing)
                geometry.append(["kind": "anchor", "screen": rect(screen), "placement": placement.rawValue,
                                 "position": [position.0.isNaN ? "nan" as Any : position.0, position.1], "scale": scale,
                                 "editing": editing, "anchor": [Double(a.x), Double(a.y)]])
            }}
        }}}
        for screen in screens + [NSRect(x: 10, y: 10, width: 0, height: 100)] { for point in points.prefix(3) {
            let p = OverlayGeometry.normalizedPosition(anchor: point, screen: OverlayScreen(id: 9, visibleFrame: screen))
            geometry.append(["kind": "normalize", "screen": rect(screen), "point": [Double(point.x), Double(point.y)],
                             "screenID": optional(p.screenID), "position": [p.x, p.y]])
        }}
        var normalized: [[String: Any]] = []
        for (duration, scale, accent) in [(3.0, 1.0, "FAD41F"), (0.2, 0.1, "#3fa9f5"), (99, 9, " 12AB9z"), (.nan, .infinity, "ABCDEF0"), (60, 1.6, "abcdef")] {
            var configuration = AppConfiguration.defaults
            configuration.displayDuration = duration; configuration.scale = scale; configuration.accentHex = accent
            configuration.customPosition = OverlayPosition(screenID: nil, x: scale, y: -scale)
            let n = configuration.normalized
            let color = n.accentColor.usingColorSpace(.sRGB)!
            normalized.append(["duration": duration.isFinite ? duration as Any : String(duration), "scale": scale.isFinite ? scale as Any : String(scale),
                               "accent": accent, "normalizedDuration": n.displayDuration, "normalizedScale": n.scale,
                               "normalizedAccent": n.accentHex, "position": [n.customPosition.x, n.customPosition.y],
                               "accentRGB": [Double(color.redComponent), Double(color.greenComponent), Double(color.blueComponent)]])
        }
        result["geometry"] = geometry
        result["normalization"] = normalized

        // H. Event recorder battery transitions; I. device readings.
        let sequence: [(String, BatterySnapshot)] = [snapshots[2], snapshots[1], snapshots[0], snapshots[4], snapshots[6], snapshots[2],
                                                     snapshots[0], snapshots[5], snapshots[2], snapshots[2], snapshots[3], snapshots[6], snapshots[6]]
        let recorder = BatteryRecorderFixture()
        var recorded: [[String: Any]] = []
        for (name, value) in sequence {
            let before = recorder.log.rows.count
            recorder.receiveBattery(value)
            recorded.append(["snapshot": name, "events": Array(recorder.log.rows[before...])])
        }
        result["recorder"] = recorded
        result["devices"] = snapshots.map { name, value -> [String: Any] in
            ["snapshot": name, "readings": DeviceBatteryProvider().readings(for: value).map { r -> [String: Any] in
                let availability: String
                switch r.availability { case .available: availability = "available"; case .unavailable: availability = "unavailable"; case .noBattery: availability = "noBattery" }
                return ["id": r.id, "kind": r.kind == .mac ? "mac" : "accessory", "name": r.name,
                        "percentage": optional(r.percentage), "availability": availability] }]
        }

        // J. AppDelegate battery bodies against an inert overlay stand-in.
        var delegateRuns: [[String: Any]] = []
        func delegateRun(_ name: String, _ steps: [(String, (AppDelegateBatteryFixture) -> Void)]) {
            let app = AppDelegateBatteryFixture()
            var rows: [[String: Any]] = []
            for (label, step) in steps {
                let before = app.overlay.log.count
                step(app)
                rows.append(["step": label, "calls": Array(app.overlay.log[before...]), "menu": app.batteryMenuItem.title,
                             "tooltip": app.statusItem?.button?.toolTip ?? "", "visible": app.overlay.isVisible,
                             "persistent": app.overlay.isPersistent,
                             "preview": app.previewSnapshot.map { OverlayStub.describe($0) } ?? "nil"])
            }
            delegateRuns.append(["name": name, "rows": rows])
        }
        func receive(_ i: Int) -> (String, (AppDelegateBatteryFixture) -> Void) { ("receive:" + snapshots[i].0, { $0.receive(snapshots[i].1) }) }
        let dismissed: (String, (AppDelegateBatteryFixture) -> Void) = ("dismissed", { $0.overlay.isVisible = false; $0.overlay.isPersistent = false })
        let preview: (String, (AppDelegateBatteryFixture) -> Void) = ("preview", { $0.preview(nil) })
        delegateRun("transitions", [receive(2), receive(2), receive(1), receive(0), receive(4), dismissed, receive(2), receive(6), receive(1), receive(2), receive(5)])
        delegateRun("previewOwnsDeadline", [receive(2), preview, receive(3), receive(1), dismissed, preview, receive(1), dismissed, receive(2)])
        delegateRun("previewWithoutBattery", [receive(6), preview, receive(6), dismissed, preview, receive(2)])
        delegateRun("alwaysMode", [("always", { $0.store.configuration.displayMode = .always; $0.hudSettings.configuration.displayMode = .always }),
                                   receive(2), receive(1), receive(1), preview, receive(6), receive(5), dismissed, preview])
        delegateRun("alertsDisabled", [("disable", { $0.store.configuration.batteryAlertsEnabled = false }), receive(2), receive(1), preview, receive(2)])
        delegateRun("hudActive", [receive(2), ("hudOpen", { $0.overlay.isSystemOverlayActive = true }), receive(1), preview,
                                  ("hudClosed", { $0.overlay.isSystemOverlayActive = false }), receive(2)])
        delegateRun("suspended", [receive(2), ("suspend", { $0.suspended = true }), receive(1), ("resume", { $0.suspended = false; $0.presentLatestSnapshot() }), receive(1)])
        delegateRun("editing", [receive(2), ("edit", { $0.overlay.isEditingPosition = true }), receive(1), preview,
                                ("editDone", { $0.overlay.isEditingPosition = false; $0.presentLatestSnapshot() })])
        for chinese in [false, true] {
            L10n.useChinese = chinese
            delegateRun(chinese ? "menuChinese" : "menuEnglish", snapshots.indices.map { receive($0) })
        }
        L10n.useChinese = false
        result["delegate"] = delegateRuns

        // L. Capacity registry pair rules used for Windows mWh/relative values.
        var capacities: [[String: Any]] = []
        let registryCases: [[String: Any]] = [[:], ["AppleRawCurrentCapacity": 3600, "AppleRawMaxCapacity": 4800],
            ["AppleRawCurrentCapacity": 4801, "AppleRawMaxCapacity": 4800, "CurrentCapacity": 50, "MaxCapacity": 100],
            ["CurrentCapacity": 2500, "MaxCapacity": 5000], ["CurrentCapacity": 101, "MaxCapacity": 101],
            ["CurrentCapacity": 100, "MaxCapacity": 100], ["AppleRawCurrentCapacity": 1.5, "AppleRawMaxCapacity": 4800],
            ["AppleRawCurrentCapacity": -1, "AppleRawMaxCapacity": 4800], ["AppleRawCurrentCapacity": 0, "AppleRawMaxCapacity": 1_000_000],
            ["AppleRawCurrentCapacity": 0, "AppleRawMaxCapacity": 1_000_001], ["AppleRawCurrentCapacity": true, "AppleRawMaxCapacity": 4800]]
        for properties in registryCases { for percentage in [nil, 0, 64, 100, 101] as [Int?] {
            let r = BatteryCapacityReading.fromRegistry(properties, percentage: percentage)
            capacities.append(["properties": properties.mapValues { "\($0)" }, "percentage": optional(percentage),
                               "result": r.map { ["current": $0.current, "maximum": $0.maximum, "unit": $0.unit.rawValue] as Any } ?? NSNull()])
        }}
        result["capacities"] = capacities
        result["snapshots"] = snapshots.map { name, value -> [String: Any] in
            var row = describe(value); row["name"] = name; row["chargeMode"] = value.isChargeMode
            row["tone"] = value.levelTone.map { tone -> String in switch tone { case .green: return "green"; case .yellow: return "yellow"; case .red: return "red" } } ?? "none"
            return row
        }
        result["liveServices"] = false
        try JSONSerialization.data(withJSONObject: jsonSafe(result), options: [.sortedKeys]).write(to: out.appendingPathComponent("reference.json"))
        print("Charge reference: \(trees.count) trees, \(timelines.count) timelines, \(readings.count) readings")
    }
}
