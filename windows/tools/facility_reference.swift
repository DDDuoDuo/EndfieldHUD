import AppKit
import QuartzCore

// Actual-source oracle of the facility alerts and the daily boot log (Mac 1.3.0
// Sources/FacilityAlertMonitor.swift, HUDFacilityBanner.swift, HUDFacilityTags.swift, HUDBootLog.swift):
// the unchanged classes run detached (no SystemHUDView, window, timer or system reader) with synthetic
// FacilityAlertSources, a manual HUDTimers and Reduce Motion on, so every state is at rest. Exported:
// the alert strings in five languages; the banner of each alert (English, Simplified Chinese, Japanese) and
// the tag stack as model layer trees with their controls and accessibility items (the design recorder,
// tools/hud_design_reference_support.swift), with every measured text width; the boot log's content lines
// (segments and colours) for full, sparse and nominal data, and the panel after every line was revealed.
// Isolation: temporary CFFIXED_USER_HOME, UTC, no window, no input, no system reading.

private let env = HUDDesignEnvironment(renderScale: 2, reduceMotion: { true })

/// HUDTimers whose work is collected and run in time order on demand.
private final class ManualTimers {
    var now: CFTimeInterval = 0
    private var items: [(CFTimeInterval, Int, DispatchWorkItem)] = []
    private var serial = 0
    var timers: HUDTimers {
        HUDTimers(now: { [unowned self] in self.now }, schedule: { [unowned self] delay, work in self.serial += 1; self.items.append((self.now + delay, self.serial, work)) })
    }
    /// Runs the next pending item (the earliest), advancing the clock to it; false when none is left.
    @discardableResult func runNext() -> Bool {
        items.removeAll { $0.2.isCancelled }
        guard let next = items.min(by: { ($0.0, $0.1) < ($1.0, $1.1) }) else { return false }
        items.removeAll { $0.1 == next.1 }
        now = max(now, next.0)
        next.2.perform()
        return true
    }
}

private struct Readings {
    var disk = FacilityDiskReading(volumeName: "Macintosh HD", totalBytes: 500_000_000_000, availableBytes: 40_000_000_000)
    var pressure: FacilityMemoryPressure? = .warning
    var usage = FacilityMemoryUsage(usedBytes: 14_720_000_000, totalBytes: 16_000_000_000)
    var thermal = FacilityThermalState.serious
    var temperature: Double? = 91.6
}
/// The alerts a monitor lists for these readings (the real FacilityAlert values).
private func alerts(_ r: Readings) -> [FacilityAlert] {
    let sources = FacilityAlertSources(
        readDisk: { r.disk }, readMemoryPressure: { r.pressure }, readMemoryUsage: { r.usage },
        readThermalState: { r.thermal }, readTemperature: { r.temperature },
        observeEvents: { _ in {} }, scheduleRepeating: { _, _ in {} }, runWorker: { $0() }, clock: { 0 })
    let monitor = FacilityAlertMonitor(sources: sources)
    monitor.isEnabled = true
    return monitor.alerts
}
private func measurement(_ alert: FacilityAlert) -> [String: Any] {
    var row: [String: Any] = ["kind": alert.kind.rawValue]
    switch alert.measurement {
    case .disk(let d): row["disk"] = ["volumeName": d.volumeName, "totalBytes": d.totalBytes, "availableBytes": d.availableBytes]
    case .memory(let m):
        row["memory"] = ["pressure": m.pressure.rawValue, "usage": m.usage.map { ["usedBytes": $0.usedBytes, "totalBytes": $0.totalBytes] as Any } ?? NSNull()]
    case .heat(let h): row["heat"] = ["state": h.state.rawValue, "temperature": h.temperatureCelsius as Any? ?? NSNull()]
    }
    return row
}
private func anchored(_ layer: CALayer) { HUDMotion.perform { layer.anchorPoint = .zero; layer.position = .zero } }

@main private enum FacilityReference {
    static func main() throws {
        precondition(CommandLine.arguments.count >= 2)
        let output = URL(fileURLWithPath: CommandLine.arguments[1])
        NSApplication.shared.setActivationPolicy(.prohibited)
        var configuration = AppConfiguration.defaults
        configuration.language = .english; configuration.theme = .dark; configuration.reduceMotion = true; configuration.accentHex = "FAD41F"
        HUDRuntimeAppearance.configuration = configuration
        L10n.language = .english
        HUDColor.apply(accentHex: "FAD41F", light: false)
        let recorder = try Recorder(output: output)
        var payload: [String: Any] = ["schema": 1]
        let languages: [AppLanguage] = [.english, .simplifiedChinese, .traditionalChinese, .japanese, .korean]

        // MARK: Alerts: every reading variant and its strings in five languages.
        var full = Readings()
        var bare = Readings(); bare.usage = nil; bare.temperature = nil; bare.thermal = .critical; bare.pressure = .critical
        bare.disk = FacilityDiskReading(volumeName: "Backup", totalBytes: 4_000_000_000_000, availableBytes: 125_000_000_000)
        let variants: [(String, Readings)] = [("full", full), ("bare", bare)]
        var strings: [[String: Any]] = []
        for (name, readings) in variants {
            for alert in alerts(readings) {
                var row: [String: Any] = ["variant": name, "input": measurement(alert)]
                for language in languages {
                    L10n.language = language
                    row[languageName(language)] = ["title": alert.title, "value": alert.value, "unit": alert.unit, "detail": alert.detail,
                                                   "tagText": alert.tagText, "tagLabel": alert.tagLabel, "tagSuffix": alert.tagSuffix as Any? ?? NSNull(),
                                                   "englishTitle": alert.englishTitle, "chineseTitle": alert.chineseTitle]
                }
                L10n.language = .english
                strings.append(row)
            }
        }
        payload["strings"] = strings
        full.temperature = 91.6

        // MARK: Banner: each alert at rest (Reduce Motion), in three languages.
        var banners: [[String: Any]] = []
        for (name, readings) in variants {
            for alert in alerts(readings) {
                for language in [AppLanguage.english, .simplifiedChinese, .japanese] {
                    L10n.language = language
                    let timers = ManualTimers()
                    let banner = HUDFacilityBanner(env: env, timers: timers.timers)
                    anchored(banner.layer)
                    banner.show(alert)
                    banners.append(["variant": name, "language": languageName(language), "input": measurement(alert),
                                    "layer": try recorder.tree(banner.layer, id: "banner"),
                                    "controls": [controlState(banner.control, in: banner.layer)],
                                    "texts": banner.textsForTesting, "counting": banner.isCountingDownForTesting,
                                    "remaining": banner.countdownRemainingForTesting])
                }
                L10n.language = .english
            }
        }
        payload["banners"] = banners

        // MARK: Tag stack: the three alerts at rest, then after the disk tag's x (Reduce Motion: removed at once).
        var stacks: [[String: Any]] = []
        for language in [AppLanguage.english, .simplifiedChinese] {
            for (name, readings) in variants {
                L10n.language = language
                let timers = ManualTimers()
                let stack = HUDFacilityTagStack(env: env, timers: timers.timers)
                let listed = alerts(readings)
                stack.update(alerts: listed, animated: false)
                stacks.append(["variant": name, "language": languageName(language), "state": "listed", "input": listed.map(measurement),
                               "layer": try recorder.tree(stack.layer, id: "tags"), "controls": stack.controls.map { controlState($0, in: stack.layer) },
                               "height": stack.preferredHeight])
                stack.controls[1].activate()
                stacks.append(["variant": name, "language": languageName(language), "state": "dismissed", "input": Array(listed.dropFirst()).map(measurement),
                               "layer": try recorder.tree(stack.layer, id: "tags"), "controls": stack.controls.map { controlState($0, in: stack.layer) },
                               "height": stack.preferredHeight])
            }
        }
        L10n.language = .english
        payload["tags"] = stacks

        // MARK: Boot log content and panel.
        var calendar = Calendar(identifier: .gregorian)
        calendar.timeZone = TimeZone(identifier: "UTC")!
        let date = calendar.date(from: DateComponents(year: 2026, month: 10, day: 19, hour: 6, minute: 45))!
        func lines(_ name: String) -> [HUDBootLog.Line] {
            switch name {
            case "full":
                let uptime: TimeInterval = 273_959 // 3 d 4 h 5 min 59 s
                return HUDBootLogContent.lines(date: date, uptime: uptime, battery: (82, true, HUDColor.green),
                    eventsToday: ["Stand-up\nweekly", "  ", "Third"], sanity: (180, 240, 271.2), workStreak: (3, 12.34),
                    disk: (8, true), operatorName: "  Perlica \n", calendar: calendar)
            case "sparse":
                return HUDBootLogContent.lines(date: date, uptime: -5, battery: nil, eventsToday: ["One"], sanity: nil, workStreak: nil,
                    disk: nil, operatorName: " ", calendar: calendar)
            default:
                return HUDBootLogContent.lines(date: date, uptime: 60, battery: (15, false, HUDColor.red), eventsToday: [],
                    sanity: (1, 2, nil), workStreak: (1, 0), disk: (40, false), operatorName: "x", calendar: calendar)
            }
        }
        func lineRow(_ line: HUDBootLog.Line) -> [String: Any] {
            var segments: [[String: Any]] = []
            for segment in line.segments { segments.append(["text": segment.text, "color": rgba(segment.color)]) }
            var runs: [[String: Any]] = []
            for run in HUDBootLog.runs(line) { runs.append(["text": run.text, "style": styleJSON(run.style)]) }
            return ["segments": segments, "runs": runs]
        }
        var content: [[String: Any]] = []
        for name in ["full", "sparse", "nominal"] {
            var rows: [[String: Any]] = []
            for line in lines(name) { rows.append(lineRow(line)) }
            content.append(["variant": name, "lines": rows])
        }
        payload["bootContent"] = content
        var panels: [[String: Any]] = []
        for name in ["full", "sparse"] {
            let timers = ManualTimers()
            let log = HUDBootLog(env: env, timers: timers.timers)
            anchored(log.layer)
            let shown = lines(name)
            log.play(shown, version: "1.3.0")
            // Every line revealed; the finish (900 ms later) is not run.
            while log.revealedCount < min(shown.count, HUDBootLog.capacity), timers.runNext() {}
            panels.append(["variant": name, "layer": try recorder.tree(log.layer, id: "boot"), "rows": log.rowTextsForTesting,
                           "cursor": rect(log.cursorFrameForTesting), "terminal": log.terminalTextForTesting, "revealed": log.revealedCount])
        }
        payload["bootPanels"] = panels

        payload["text"] = ["widths": Array(recorder.widths.values), "metrics": Array(recorder.metrics.values)]
        payload["layerDefaults"] = recorder.layerDefaults
        let data = try JSONSerialization.data(withJSONObject: sanitize(payload), options: [.sortedKeys])
        try data.write(to: output.appendingPathComponent("facility-source.json"))
        print("Exported \(strings.count) alerts, \(banners.count) banners, \(stacks.count) tag stacks, \(panels.count) boot log panels")
    }
}
