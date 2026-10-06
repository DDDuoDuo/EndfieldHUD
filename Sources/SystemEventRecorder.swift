import AppKit

/// Records state transitions, never a timer tick or global user activity.
/// Initial snapshots establish baselines without inventing connection events.
final class SystemEventRecorder {
    let log: SystemEventLog
    private var battery: BatterySnapshot?
    private var work: WorkModeSnapshot?
    private var audioDevices: [String: String]?
    private var displays: [String: String]?
    private struct DisplaySettings: Equatable {
        let clockStyle: HUDClockStyle
        let centerLogo: HUDCenterLogo
        let centerLogoRevision: String?
        let alertMetric: HUDChargeMetric
    }
    private var displaySettings: DisplaySettings?
    private var profileCrop: (background: Double, thumbnail: Double)?

    init(log: SystemEventLog) { self.log = log }

    /// Called only from committed configuration publications. The first value
    /// establishes a baseline, and revision IDs are kept in memory only.
    func receiveConfiguration(_ value: AppConfiguration) {
        precondition(Thread.isMainThread)
        let next = DisplaySettings(clockStyle: value.clockStyle, centerLogo: value.centerLogo,
                                   centerLogoRevision: value.centerLogoRevision, alertMetric: value.alertMetric)
        let previous = displaySettings; displaySettings = next
        guard let previous else { return }
        if previous.clockStyle != next.clockStyle {
            log.record(kind: .displaySettingsChanged, metadata: ["field": "clockStyle", "value": next.clockStyle.rawValue])
        }
        if previous.centerLogo != next.centerLogo
            || (next.centerLogo == .custom && previous.centerLogoRevision != next.centerLogoRevision) {
            let imported = next.centerLogo == .custom && next.centerLogoRevision != nil
                && previous.centerLogoRevision != next.centerLogoRevision
            log.record(kind: .displaySettingsChanged, metadata: ["field": "centerLogo", "value": imported ? "customImported" : next.centerLogo.rawValue])
        }
        if previous.alertMetric != next.alertMetric {
            log.record(kind: .displaySettingsChanged, metadata: ["field": "alertMetric", "value": next.alertMetric.rawValue])
        }
    }

    /// Feed committed profile snapshots, never crop drafts or mouse movement.
    /// Store only which crop changed; zoom values and user content are omitted.
    func receiveProfileCrop(backgroundZoom: Double, thumbnailZoom: Double) {
        precondition(Thread.isMainThread)
        func normalized(_ value: Double) -> Double { value.isFinite ? min(20, max(1, value)) : 1 }
        let next = (background: normalized(backgroundZoom), thumbnail: normalized(thumbnailZoom))
        let previous = profileCrop; profileCrop = next
        guard let previous else { return }
        let background = previous.background != next.background, thumbnail = previous.thumbnail != next.thumbnail
        guard background || thumbnail else { return }
        log.record(kind: .profileCropChanged, metadata: ["target": background && thumbnail ? "both" : background ? "background" : "thumbnail"])
    }

    func receiveBattery(_ value: BatterySnapshot) {
        precondition(Thread.isMainThread)
        let previous = battery; battery = value
        guard let previous else { return }
        var metadata = ["state": Self.batteryState(value)]
        if let percentage = value.percentage { metadata["percentage"] = String(percentage) }
        // Missing battery data is not proof that a power cable was removed.
        if previous.hasBattery && value.hasBattery && previous.isPluggedIn != value.isPluggedIn {
            log.record(kind: value.isPluggedIn ? .powerConnected : .powerDisconnected, metadata: metadata)
        }
        if Self.batteryState(previous) != Self.batteryState(value) {
            log.record(kind: .batteryStateChanged, metadata: metadata)
        }
    }

    func receiveWork(_ value: WorkModeSnapshot) {
        precondition(Thread.isMainThread)
        let previous = work; work = value
        guard let previous, previous.phase != value.phase else { return }
        let metadata = Self.workMetadata(value.phase == .idle ? previous : value)
        switch value.phase {
        case .running: log.record(kind: previous.phase == .paused ? .workResumed : .workStarted, metadata: metadata)
        case .paused: log.record(kind: .workPaused, metadata: metadata)
        case .completed: log.record(kind: .workCompleted, metadata: metadata)
        case .idle:
            if previous.isActive || previous.phase == .completed || previous.phase == .stopped {
                log.record(kind: .workReset, metadata: metadata)
            }
        case .stopped: break // No Stop action is exposed in the HUD.
        }
    }

    func receiveAudioDevices(_ devices: [String: String]) {
        precondition(Thread.isMainThread)
        let previous = audioDevices; audioDevices = devices
        recordTopology(previous: previous, current: devices, connected: .audioDeviceConnected, disconnected: .audioDeviceDisconnected)
    }

    func receiveDisplays(_ devices: [String: String]) {
        precondition(Thread.isMainThread)
        let previous = displays; displays = devices
        recordTopology(previous: previous, current: devices, connected: .displayConnected, disconnected: .displayDisconnected)
    }

    func refreshDisplays() {
        receiveDisplays(Dictionary(uniqueKeysWithValues: NSScreen.screens.compactMap { screen in
            guard let id = screen.deviceDescription[NSDeviceDescriptionKey("NSScreenNumber")] as? NSNumber else { return nil }
            return (id.stringValue, screen.localizedName)
        }))
    }

    private func recordTopology(previous: [String: String]?, current: [String: String],
                                connected: SystemEventKind, disconnected: SystemEventKind) {
        guard let previous else { return }
        // Persist human labels only; hardware identifiers are used solely to
        // deduplicate the in-memory device snapshot and never enter the log.
        for id in previous.keys.sorted() where current[id] == nil {
            log.record(kind: disconnected, metadata: ["device": previous[id]!])
        }
        for id in current.keys.sorted() where previous[id] == nil {
            log.record(kind: connected, metadata: ["device": current[id]!])
        }
    }

    private static func batteryState(_ value: BatterySnapshot) -> String {
        if !value.hasBattery { return "unavailable" }
        if value.isCharging { return "charging" }
        if value.isFullyCharged { return "full" }
        return value.isPluggedIn ? "connected" : "battery"
    }

    private static func workMetadata(_ value: WorkModeSnapshot) -> [String: String] {
        var result = ["kind": value.kind.rawValue]
        if value.kind == .countdown, value.duration.isFinite {
            result["seconds"] = String(Int(min(86_400, max(1, value.duration))))
        }
        return result
    }
}
