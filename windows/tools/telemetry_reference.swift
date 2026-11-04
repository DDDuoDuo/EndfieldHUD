import AppKit
import IOKit.ps
import QuartzCore

// Actual-source oracle of Storage, Activity Monitor and Battery (Mac 1.3.0 Sources/TelemetryCanvases.swift,
// ActivityMonitorCanvas.swift, BatteryCanvas.swift, StorageController.swift, BatteryCapacity.swift,
// AppActivityMonitor.swift, SystemActivityMonitor.swift) for windows/tests/telemetry_canvas_tests.cpp:
// the number formats, the Overview graph paths, the level tone and diagnostics rules, the app derivation's
// threads, the StorageController's disk choice script, and each canvas built by the unchanged Mac code in
// scripted states (English and Simplified Chinese): its accessibility items, its visible text layers (runs,
// styles, colours, line boxes in the content box) and the widths CoreText measured for them (the Windows test's
// recorded-width measurer). Isolation: temporary CFFIXED_USER_HOME, no window, no input, no timer outliving a
// step, no store, no disk/battery/process of this Mac read (every reader is injected).

// JSON helpers (round6, rect, hex, pathJSON) and the measured widths / line metrics come from the shared
// tools/hud_design_reference_support.swift recorder.
private func optional<T>(_ v: T?) -> Any { v.map { $0 as Any } ?? NSNull() }
private let env = HUDDesignEnvironment(renderScale: 2, reduceMotion: { false })

/// The widths CoreText measured for every shown text (the Windows test's recorded-width measurer).
private var recorder: Recorder!
private func visible(_ layer: CALayer, under root: CALayer) -> Bool {
    var l: CALayer? = layer
    while let current = l, current !== root { if current.isHidden || current.opacity < 0.01 { return false }; l = current.superlayer }
    return true
}
private func texts(_ root: CALayer) -> [[String: Any]] {
    var out: [[String: Any]] = []
    func walk(_ layer: CALayer) {
        if let t = layer as? HUDTextLayer, !t.text.isEmpty, visible(t, under: root), let parent = t.superlayer {
            let language = t.language ?? HUDTextLanguage.tag()
            recorder.width(t.runs, language: language)
            // The text's own runs measured too (BigNumeral / chips / buttons ask per run set).
            out.append(["text": t.text, "frame": rect(root.convert(t.lineFrame, from: parent)), "alignment": "\(t.alignment)",
                        "maximumLines": t.maximumLines, "minimumScale": t.minimumScale, "language": optional(t.language),
                        "runs": t.runs.map { ["text": $0.text, "size": $0.style.size, "weight": $0.style.weight.resolved.rawValue, "color": hex($0.style.color),
                                              "lineHeight": $0.style.lineHeight, "tracking": $0.style.tracking, "uppercase": $0.style.uppercase,
                                              "tabular": $0.style.tabular] }])
        }
        (layer.sublayers ?? []).forEach(walk)
    }
    walk(root)
    return out
}
private func state(_ name: String, _ canvas: HUDDesignCanvas, _ extra: [String: Any] = [:]) -> [String: Any] {
    var row: [String: Any] = ["name": name, "language": L10n.language.rawValue, "accessibility": accessibility(canvas.accessibilityItems),
                              "texts": texts(canvas.root), "controls": canvas.page.controls.map(\.id)]
    for (k, v) in extra { row[k] = v }
    return row
}

@main private enum TelemetryReference {
    static func main() throws {
        guard CommandLine.arguments.count >= 2 else { fatalError("Pass a NEW output directory") }
        let output = URL(fileURLWithPath: CommandLine.arguments[1])
        recorder = try Recorder(output: output)
        NSApplication.shared.setActivationPolicy(.prohibited)
        HUDColor.apply(accentHex: "FAD41F", light: false)
        L10n.language = .english
        var payload: [String: Any] = ["schema": 1]

        // MARK: TelemetryFormat
        let numbers: [Double?] = [nil, -1, 0, 1, 99.5, 99.94, 100, 999, 1000, 1250, 12000, 99940, 100000, 400000, 1234567890, 82_400_000_000, 1e18, .nan]
        payload["formats"] = numbers.map { v -> [String: Any] in
            ["value": v.map { $0.isNaN ? "nan" as Any : $0 as Any } ?? NSNull(), "bytes": TelemetryFormat.bytes(v), "rate": TelemetryFormat.rate(v),
             "percent": TelemetryFormat.percent(v), "percentUnlimited": TelemetryFormat.percent(v, limit: nil)]
        }
        payload["storageFormats"] = [Int64(-5), 0, 999_000, 59_500_000_000, 994_700_000_000, 999_960_000_000, 1_000_000_000_000, 1_864_600_000_000].map {
            let s = TelemetryFormat.storage($0); return ["bytes": $0, "number": s.number, "unit": s.unit] as [String: Any]
        }
        payload["memoryFormats"] = [UInt64?.none, 0, 82_000_000, 720_000_000, 999_999_999, 1_000_000_000, 1_234_000_000].map {
            ["bytes": optional($0), "memory": TelemetryFormat.memory($0)] as [String: Any]
        }
        payload["durations"] = [Int?.none, -1, 0, 11, 59, 60, 377, 6000].map { ["minutes": optional($0), "text": TelemetryFormat.duration(minutes: $0)] as [String: Any] }

        // MARK: ActivityGraph.paths
        func series(_ count: Int, _ f: (Int) -> Double?) -> [Double?] { (0..<count).map(f) }
        let graphCases: [[Double?]] = [series(0) { _ in 1 }, series(1) { _ in 5 }, series(7) { [nil, 10, 20, nil, 30, 40, 150][$0] },
                                       series(44) { Double($0 * 3 % 101) }, series(60) { $0 % 9 == 4 ? nil : 50 + sin(Double($0)) * 40 },
                                       series(12) { $0 == 5 ? .infinity : Double($0) * -3 + 20 }]
        payload["graphs"] = graphCases.map { values -> [String: Any] in
            let p = ActivityGraph.paths(values, size: ActivityGraph.size)
            return ["values": values.map { $0.map { $0.isFinite ? $0 as Any : "inf" as Any } ?? NSNull() }, "line": pathJSON(p.line), "area": pathJSON(p.area)] as [String: Any]
        }

        // MARK: BatteryLevelTone and BatteryDiagnosticsParsing
        payload["tones"] = ([nil, -1] + Array(0...100) + [101]).map { level -> [String: Any] in
            let tone = BatteryLevelTone.forPercentage(level)
            return ["level": optional(level), "tone": tone.map { "\($0)" } ?? NSNull()]
        }
        let diagnostics: [Any?] = [nil, NSNumber(value: true), NSNumber(value: -1), NSNumber(value: 0), NSNumber(value: 1.5), NSNumber(value: 318),
                                   NSNumber(value: 6000), NSNumber(value: 6001), NSNumber(value: 100_000), NSNumber(value: 100_001), NSNumber(value: 1_000_001),
                                   NSNumber(value: 3125), NSNumber(value: -4001), NSNumber(value: 15000), NSNumber(value: 15001), "12"]
        payload["diagnostics"] = diagnostics.map { value -> [String: Any] in
            let input: Any = (value as? NSNumber).map { CFGetTypeID($0) == CFBooleanGetTypeID() ? "bool" as Any : $0.doubleValue as Any } ?? (value == nil ? NSNull() : "string")
            return ["input": input, "cycleCount": optional(BatteryDiagnosticsParsing.cycleCount(value)),
                    "temperature": optional(BatteryDiagnosticsParsing.temperatureCelsius(value)),
                    "watts": optional(BatteryDiagnosticsParsing.adapterWatts(value.map { [kIOPSPowerAdapterWattsKey: $0] })),
                    "minutes": optional(BatteryDiagnosticsParsing.minutes(value))]
        }

        // MARK: System and app derivations (the former activity_reference.py cases, unchanged Mac rules)
        func cpu(_ a: [UInt64]) -> SystemActivityCPUTicks { SystemActivityCPUTicks(user: a[0], system: a[1], idle: a[2], nice: a[3]) }
        var cpuCases: [[String: Any]] = []
        for n in 0..<80 {
            let old: [UInt64] = [100, 40, 600, 3], current: [UInt64] = [100 + UInt64(n), 40 + UInt64(n * 2), 600 + UInt64(n % 17), 3 + UInt64(n % 4)]
            cpuCases.append(["old": old, "now": current, "percent": optional(SystemActivityDerivation.cpuPercent(cpu(current), cpu(old)))])
        }
        cpuCases.append(["old": [100, 40, 600, 3], "now": [99, 41, 601, 3], "percent": NSNull()])
        var memoryCases: [[String: Any]] = []
        for n in 0..<40 {
            let args: [UInt64] = [100000, 4096, UInt64(n), UInt64(n % 5), 3, 2]
            let value = SystemActivityMemoryCounters(physicalBytes: args[0], pageSize: args[1], internalPages: args[2], purgeablePages: args[3],
                                                     wiredPages: args[4], compressedPages: args[5]).bytes
            memoryCases.append(["args": args, "result": value.map { [$0.used, $0.total, $0.compressed] as Any } ?? NSNull()])
        }
        func snap(_ s: SystemActivitySnapshot) -> [String: Any] {
            ["cpu": optional(s.cpuPercent), "upload": optional(s.uploadBytesPerSecond), "download": optional(s.downloadBytesPerSecond),
             "read": optional(s.diskReadBytesPerSecond), "write": optional(s.diskWriteBytesPerSecond), "notes": s.statusNotes]
        }
        var rateCases: [[String: Any]] = []
        for elapsed in [0.0, 0.5, 1, 5, 15, 15.0001, 30] { for topology in [false, true] {
            let old = SystemActivityRawSample(timestamp: Date(timeIntervalSince1970: 1), uptime: 100, cpu: cpu([1, 2, 3, 4]), memory: nil,
                                              network: ["en0": SystemActivityByteCounters(received: 10, sent: 20)], disk: [:])
            let current = SystemActivityRawSample(timestamp: Date(timeIntervalSince1970: 2), uptime: 100 + elapsed, cpu: cpu([2, 4, 6, 8]), memory: nil,
                                                  network: [topology ? "en1" : "en0": SystemActivityByteCounters(received: 30, sent: 70)], disk: [:])
            rateCases.append(["elapsed": elapsed, "topology": topology, "result": snap(SystemActivityDerivation.snapshot(current, previous: old))])
        }}
        var appCases: [[String: Any]] = []
        for mode in 0..<8 {
            let app = AppActivityIdentity(id: "app", name: "Example", bundleIdentifier: nil, bundleURL: nil, processIDs: [8, 7, 7])
            func process(_ pid: Int32, _ start: UInt64, _ ticks: UInt64, _ ram: UInt64, _ read: UInt64, _ write: UInt64) -> AppActivityProcessReading {
                AppActivityProcessReading(pid: pid, startID: start, startUptime: 1, cpuTicks: ticks, memoryBytes: ram, diskReadBytes: read, diskWriteBytes: write)
            }
            let old = AppActivityRawSample(timestamp: Date(timeIntervalSince1970: 1), uptime: 10, apps: [app], members: ["app": [7, 8]],
                                           processes: [7: process(7, 1, 100, 1000, 100, 100), 8: process(8, 2, 200, 2000, 200, 200)], secondsPerCPUTick: 0.01, network: nil, statusNotes: [])
            var processes: [Int32: AppActivityProcessReading] = [7: process(7, 1, 250, 1000, 150, 100), 8: process(8, mode == 2 ? 3 : 2, 300, 2000, 200, 300)]
            if mode == 1 { processes.removeValue(forKey: 8) }
            let current = mode == 6 ? 15.0 : mode == 7 ? 15.0001 : 11.0
            let network = AppActivityNetworkReading(uptime: mode == 5 ? 6 : current, intervalStart: 5, rates: [7: AppActivityNetworkRates(received: 10, sent: 20)],
                                                    unknownPIDs: mode == 4 ? [8] : [])
            let raw = AppActivityRawSample(timestamp: Date(timeIntervalSince1970: 2), uptime: current, apps: [app], members: ["app": mode == 3 ? [7] : [7, 8]],
                                           processes: processes, secondsPerCPUTick: 0.01, network: network, statusNotes: [])
            let value = AppActivityDerivation.snapshot(raw, previous: old).items[0]
            appCases.append(["mode": mode, "cpu": optional(value.cpuPercent), "memory": optional(value.memoryBytes), "read": optional(value.diskReadBytesPerSecond),
                             "write": optional(value.diskWriteBytesPerSecond), "upload": optional(value.uploadBytesPerSecond),
                             "download": optional(value.downloadBytesPerSecond), "notes": value.statusNotes])
        }
        payload["derivations"] = ["cpu": cpuCases, "memory": memoryCases, "rates": rateCases, "apps": appCases]

        // MARK: App threads (AppActivityDerivation)
        let app = AppActivityIdentity(id: "app", name: "Example", bundleIdentifier: nil, bundleURL: nil, processIDs: [7, 8])
        func reading(_ pid: Int32, _ threads: Int?) -> AppActivityProcessReading {
            AppActivityProcessReading(pid: pid, startID: UInt64(pid), startUptime: 1, cpuTicks: 100, memoryBytes: 1000, diskReadBytes: 0, diskWriteBytes: 0, threads: threads)
        }
        payload["threads"] = [(Int?(4), Int?(9), true), (4, nil, true), (4, 9, false), (0, 0, true)].map { a, b, complete -> [String: Any] in
            let raw = AppActivityRawSample(timestamp: Date(timeIntervalSince1970: 2), uptime: 11, apps: [app], members: ["app": [7, 8]],
                                           processes: [7: reading(7, a), 8: reading(8, b)], secondsPerCPUTick: 0.01, network: nil, statusNotes: [],
                                           inventoryComplete: complete)
            let item = AppActivityDerivation.snapshot(raw, previous: nil).items[0]
            return ["a": optional(a), "b": optional(b), "complete": complete, "threads": optional(item.threads)]
        }

        // MARK: StorageController: the disk choice
        var now = 0.0, workers: [() -> Void] = []
        var listed: [StorageVolume] = []
        func volume(_ id: String, _ name: String, _ total: Int64, _ available: Int64, startup: Bool, onboard: Bool = true) -> StorageVolume {
            StorageVolume(id: id, url: URL(fileURLWithPath: "/Volumes/" + id), isStartup: startup, isInternal: onboard,
                          capacity: StorageCapacity(volumeName: name, totalBytes: total, availableBytes: available, updatedAt: Date(timeIntervalSince1970: now))!)
        }
        let center = NotificationCenter()
        let suite = "EndfieldHUD.TelemetryReference.\(ProcessInfo.processInfo.processIdentifier)"
        let defaults = UserDefaults(suiteName: suite)!
        defaults.removePersistentDomain(forName: suite)
        let controller = StorageController(clock: { Date(timeIntervalSince1970: now) }, readCapacity: { nil }, readVolumes: { listed },
                                           scanDetails: { _ in StorageDetailsSnapshot(categories: [], isLoading: false, updatedAt: nil, isPartial: false, error: nil) },
                                           runWorker: { workers.append($0) }, scheduleTimer: { _, _ in StorageOracleTimer() }, defaults: defaults, volumeEvents: center)
        var revision = 0
        let observer = controller.observe { revision += 1 }
        var storageRows: [[String: Any]] = []
        func step(_ action: String, _ t: Double, _ body: () -> Void) {
            now = t; body()
            let s = controller.snapshot
            storageRows.append(["action": action, "now": t, "revision": revision, "workers": workers.count, "loading": s.isLoading, "error": optional(s.error),
                                "capacity": s.capacity.map { ["name": $0.volumeName, "total": $0.totalBytes, "available": $0.availableBytes] as [String: Any] } ?? NSNull(),
                                "volumes": controller.volumes.map(\.id), "current": optional(controller.currentVolume?.id),
                                "selected": optional(controller.selectedVolumeIdentifier), "saved": optional(defaults.string(forKey: StorageController.volumeSelectionKey))])
        }
        func work() { if !workers.isEmpty { workers.removeFirst()() } }
        let first: [StorageVolume] = [volume("sys", "System", 1000, 350, startup: true), volume("data", "Data", 2000, 1500, startup: false),
                                      volume("card", "EF-CARD", 60, 47, startup: false, onboard: false)]
        listed = first
        step("activate", 0) { controller.activate() }
        step("work", 1) { work() }
        step("select data", 2) { _ = controller.selectVolume(identifier: "data") }
        step("select data again", 2.5) { _ = controller.selectVolume(identifier: "data") }
        step("select missing", 2.6) { _ = controller.selectVolume(identifier: "missing") }
        step("work", 3) { work() }
        listed = [volume("sys", "System", 1000, 340, startup: true)]
        step("refresh", 4) { controller.refresh() }
        step("unmount during read", 4.5) { center.post(name: NSWorkspace.didUnmountNotification, object: nil) }
        step("work", 5) { work() }
        listed = [volume("sys", "System", 1000, 330, startup: true), volume("data", "Data", 2000, 1400, startup: false)]
        step("work", 6) { work() }
        step("deactivate", 7) { controller.deactivate() }
        step("mount hidden", 8) { center.post(name: NSWorkspace.didMountNotification, object: nil) }
        step("activate", 9) { controller.activate() }
        step("work", 10) { work() }
        listed = []
        step("refresh", 11) { controller.refresh() }
        step("work", 12) { work() }
        controller.removeObserver(observer)
        controller.deactivate()
        defaults.removePersistentDomain(forName: suite)
        payload["storageController"] = storageRows
        // The capacity cache, hidden completion and explicit details script (the former
        // tools/storage_state_reference.swift, now against 1.3.0: a lone injected readCapacity lists no disks,
        // so the startup reader stands in).
        var stateNow = 0.0, stateWorkers: [() -> Void] = [], stateTimers: [StorageStateTimer] = [], reads = 0, stateRevision = 0
        let cache = StorageController(clock: { Date(timeIntervalSince1970: stateNow) }, readCapacity: {
            reads += 1
            return reads == 2 ? nil : StorageCapacity(volumeName: "Owned startup fixture", totalBytes: 1_000_000, availableBytes: Int64(400_000 + reads),
                                                      updatedAt: Date(timeIntervalSince1970: stateNow))
        }, scanDetails: { token in
            StorageDetailsSnapshot(categories: [StorageCategoryUsage(id: "documents", title: "Documents", bytes: 12345, isPartial: token.isCancelled)], isLoading: false,
                                   updatedAt: Date(timeIntervalSince1970: stateNow), isPartial: token.isCancelled, error: nil)
        }, runWorker: { stateWorkers.append($0) }, scheduleTimer: { _, callback in let timer = StorageStateTimer(callback); stateTimers.append(timer); return timer })
        let stateObserver = cache.observe { stateRevision += 1 }
        var stateRows: [[String: Any]] = []
        for (action, time) in [("activate", 0.0), ("activate", 0), ("refresh", 0), ("details", 0), ("work", 1), ("deactivate", 2), ("activate", 3), ("details", 3),
                               ("work", 4), ("work", 5), ("details", 6), ("wake", 63), ("refresh", 64), ("deactivate", 65), ("work", 66), ("activate", 67),
                               ("details", 67), ("details-force", 68), ("work", 69), ("details", 968), ("details", 969), ("work", 970), ("details", -1), ("work", -1),
                               ("work", -1), ("deactivate", 0), ("refresh", 1), ("details", 1)] {
            stateNow = time
            switch action {
            case "activate": cache.activate()
            case "deactivate": cache.deactivate()
            case "refresh": cache.refresh()
            case "details": cache.requestDetails()
            case "details-force": cache.requestDetails(refresh: true)
            case "work": if !stateWorkers.isEmpty { stateWorkers.removeFirst()() }
            case "wake": stateTimers.filter(\.alive).forEach { $0.callback() }
            default: fatalError("Unknown step")
            }
            let c = cache.snapshot, d = cache.details
            stateRows.append(["action": action, "now": time, "active": cache.isActive, "revision": stateRevision, "workers": stateWorkers.count,
                              "timers": stateTimers.filter(\.alive).count,
                              "capacity": c.capacity.map { ["name": $0.volumeName, "total": $0.totalBytes, "available": $0.availableBytes, "date": $0.updatedAt.timeIntervalSince1970] as [String: Any] } ?? NSNull(),
                              "loading": c.isLoading, "error": optional(c.error),
                              "details": ["categories": d.categories.map { ["id": $0.id, "title": $0.title, "bytes": optional($0.bytes), "partial": $0.isPartial] as [String: Any] },
                                          "loading": d.isLoading, "date": d.updatedAt.map { $0.timeIntervalSince1970 as Any } ?? NSNull(), "partial": d.isPartial, "error": optional(d.error)] as [String: Any]])
        }
        cache.removeObserver(stateObserver)
        payload["storageState"] = stateRows
        let many = (0..<9).map { volume("v\($0)", "V\($0)", 10, 5, startup: $0 == 0) }
        payload["limited"] = [Optional("v8"), "v2", nil, "missing"].map { keep -> [String: Any] in
            ["keeping": optional(keep), "ids": StorageVolumeCatalog.limited(many, keeping: keep).map(\.id)]
        }

        // MARK: The canvases
        let overlay = HUDCenterOverlay(env: env, schedule: { _, _ in })
        let context = HUDDesignContext(env: env, overlay: overlay)
        let style = HUDModuleContentStyle(dark: true, accent: .yellow, contentsScale: 2)
        var canvases: [[String: Any]] = []
        for language in [AppLanguage.english, .simplifiedChinese] {
            L10n.language = language
            // Battery
            let battery = BatteryCanvas(context: context)
            var configuration = AppConfiguration.defaults
            func show(_ name: String, _ snapshot: BatterySnapshot, _ metric: HUDChargeMetric) {
                configuration.alertMetric = metric
                battery.update(snapshot: snapshot, configuration: configuration)
                canvases.append(state("battery." + name, battery, ["pulsing": battery.liveAnimationCount]))
            }
            battery.update(snapshot: .unavailable, configuration: configuration)
            _ = battery.makeContent(for: .power, style: style)
            battery.setActive(true)
            show("charging", BatterySnapshot(percentage: 63, isPluggedIn: true, isCharging: true, isFullyCharged: false, hasBattery: true,
                                             capacity: BatteryCapacityReading(current: 3262, maximum: 4380, unit: .milliampHours), healthCategory: "Good",
                                             cycleCount: 1234, temperatureCelsius: 31.25, adapterWatts: 96, minutesToFull: 42, minutesToEmpty: nil), .gpu)
            show("low", BatterySnapshot(percentage: 15, isPluggedIn: false, isCharging: false, isFullyCharged: false, hasBattery: true,
                                        capacity: BatteryCapacityReading(current: 15, maximum: 100, unit: .percent), healthCategory: "Service Battery",
                                        cycleCount: nil, temperatureCelsius: 85.26, adapterWatts: nil, minutesToFull: nil, minutesToEmpty: 377), .battery)
            show("estimating", BatterySnapshot(percentage: 71, isPluggedIn: true, isCharging: true, isFullyCharged: false, hasBattery: true,
                                               healthCategory: "Fair"), .cpu)
            show("paused", BatterySnapshot(percentage: 80, isPluggedIn: true, isCharging: false, isFullyCharged: false, hasBattery: true), .ram)
            show("full", BatterySnapshot(percentage: 100, isPluggedIn: true, isCharging: false, isFullyCharged: true, hasBattery: true,
                                         healthCategory: "Odd report"), .network)
            show("none", .unavailable, .disk)
            battery.setActive(false)
            canvases.append(state("battery.inactive", battery, ["pulsing": battery.liveAnimationCount]))

            // Storage
            var storageWorkers: [() -> Void] = []
            var disks: [StorageVolume] = [volume("fixture-startup", "Macintosh HD", 1_000_000_000_000, 420_000_000_000, startup: true),
                         volume("fixture-backup", "Backup SSD", 1_864_600_000_000, 1_160_000_000_000, startup: false),
                         volume("fixture-card", "EF-CARD", 59_500_000_000, 46_700_000_000, startup: false, onboard: false)]
            var readNil = false
            let storageController = StorageController(clock: { Date(timeIntervalSince1970: 1) }, readCapacity: { nil }, readVolumes: { readNil ? [] : disks },
                                                      runWorker: { storageWorkers.append($0) }, scheduleTimer: { _, _ in StorageOracleTimer() },
                                                      defaults: nil, volumeEvents: NotificationCenter())
            let storage = StorageCanvas(context: context, controller: storageController)
            _ = storage.makeContent(for: .storage, style: style)
            storage.setActive(true)
            canvases.append(state("storage.loading", storage))
            while !storageWorkers.isEmpty { storageWorkers.removeFirst()() }
            canvases.append(state("storage.startup", storage, ["cells": [storage.cellFill.filled, storage.cellFill.low ? 1 : 0]]))
            storage.perform(actionID: "storage:refresh")
            while !storageWorkers.isEmpty { storageWorkers.removeFirst()() }
            canvases.append(state("storage.refreshed", storage, ["toast": overlay.toastState.current.map { [$0.title, $0.caption] } ?? NSNull()]))
            overlay.clearToast()
            _ = storageController.selectVolume(identifier: "fixture-backup")
            storage.navigate(dx: 0, dy: 1)
            canvases.append(state("storage.backup", storage, ["cells": [storage.cellFill.filled, storage.cellFill.low ? 1 : 0]]))
            disks[1] = volume("fixture-backup", "Backup SSD", 1_864_600_000_000, 100_000_000_000, startup: false)
            while !storageWorkers.isEmpty { storageWorkers.removeFirst()() }
            canvases.append(state("storage.low", storage, ["cells": [storage.cellFill.filled, storage.cellFill.low ? 1 : 0]]))
            readNil = true
            storage.perform(actionID: "storage:refresh")
            while !storageWorkers.isEmpty { storageWorkers.removeFirst()() }
            canvases.append(state("storage.error", storage, ["toast": overlay.toastState.current.map { [$0.title, $0.caption] } ?? NSNull()]))
            overlay.clearToast()
            storage.setActive(false)

            // Activity Monitor
            let system = SystemActivityMonitor.fixture()
            let apps = AppActivityMonitor.fixture()
            let activity = ActivityMonitorCanvas(context: context, controller: system, apps: apps)
            _ = activity.makeContent(for: .activityMonitor, style: style)
            activity.setActive(true)
            canvases.append(state("activity.overview", activity))
            activity.perform(actionID: "activity:apps")
            canvases.append(state("activity.apps", activity))
            activity.perform(actionID: "activity:sort:name")
            canvases.append(state("activity.byName", activity))
            activity.perform(actionID: "activity:sort:threads")
            canvases.append(state("activity.byThreads", activity))
            activity.setActive(false)
            var many: [AppActivityItem] = []
            for n in 0..<12 {
                let pid = Int32(100 + n), cpu = Double((n * 7) % 11), memory = UInt64(n + 1) * 100_000_000, gpu = Double(n % 4), threads = 6 + n
                let item = AppActivityItem(id: "app\(n)", name: "App \(n)", bundleIdentifier: nil, bundleURL: nil, processIDs: [pid],
                                           cpuPercent: cpu, memoryBytes: memory, uploadBytesPerSecond: nil, downloadBytesPerSecond: nil,
                                           diskReadBytesPerSecond: nil, diskWriteBytesPerSecond: nil, statusNotes: [], gpuPercent: gpu, threads: threads)
                many.append(item)
            }
            let longApps = AppActivityMonitor.fixture(items: many)
            let long = ActivityMonitorCanvas(context: context, controller: system, apps: longApps)
            _ = long.makeContent(for: .activityMonitor, style: style)
            long.setActive(true)
            long.perform(actionID: "activity:apps")
            canvases.append(state("activity.pool", long))
            _ = long.handleScroll(at: CGPoint(x: 100, y: 300), delta: CGPoint(x: 0, y: 92), event: nil)
            canvases.append(state("activity.scrolled", long))
            long.setActive(false)
            let emptyApps = AppActivityMonitor.fixture(items: [])
            let empty = ActivityMonitorCanvas(context: context, controller: system, apps: emptyApps)
            _ = empty.makeContent(for: .activityMonitor, style: style)
            empty.setActive(true)
            empty.perform(actionID: "activity:apps")
            canvases.append(state("activity.empty", empty))
            empty.setActive(false)
        }
        L10n.language = .english
        payload["canvases"] = canvases
        // The compact app rows' micro tag (drawn as cached artwork, sized by its text).
        recorder.width("ENDFIELD", HUDType.microTag, language: "zh-Hans")
        // The module header's title and subtitle line metrics (its baselines) in every region.
        for language in HUDTextLanguage.regions { _ = recorder.lineMetrics(HUDType.moduleTitle, language: language); _ = recorder.lineMetrics(HUDType.titleSubtitle, language: language) }
        payload["text"] = ["widths": recorder.widths.keys.sorted().map { recorder.widths[$0]! }, "metrics": recorder.metrics.keys.sorted().map { recorder.metrics[$0]! }]
        try FileManager.default.createDirectory(at: output, withIntermediateDirectories: true)
        let data = try JSONSerialization.data(withJSONObject: sanitize(payload), options: [.sortedKeys, .withoutEscapingSlashes])
        try data.write(to: output.appendingPathComponent("telemetry-source.json"), options: .withoutOverwriting)
        print("Exported \(canvases.count) canvas states, \(recorder.widths.count) measured widths")
    }
}

private final class StorageOracleTimer: StorageRefreshTimer { func invalidate() {} }
private final class StorageStateTimer: StorageRefreshTimer {
    var alive = true
    let callback: () -> Void
    init(_ callback: @escaping () -> Void) { self.callback = callback }
    func invalidate() { alive = false }
}
