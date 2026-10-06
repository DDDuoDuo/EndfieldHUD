import Foundation

enum SystemEventRecorderTests {
    static func run() -> Int {
        var count = 0
        func check(_ condition: Bool, _ message: String, file: StaticString = #file, line: UInt = #line) {
            count += 1; if !condition { fatalError(message, file: file, line: line) }
        }
        func battery(_ percent: Int?, plugged: Bool = false, charging: Bool = false,
                     full: Bool = false, health: String? = nil) -> BatterySnapshot {
            BatterySnapshot(percentage: percent, isPluggedIn: plugged, isCharging: charging,
                isFullyCharged: full, hasBattery: true, healthCategory: health)
        }
        let powerLog = SystemEventLog()
        let power = SystemEventRecorder(log: powerLog)
        power.receiveBattery(battery(35))
        check(powerLog.events.isEmpty, "The first battery snapshot establishes a quiet baseline")
        for percent in 36...65 { power.receiveBattery(battery(percent, health: "Normal")) }
        power.receiveBattery(battery(nil, health: "Service Recommended"))
        check(powerLog.events.isEmpty, "Battery percentages, missing percentage and health updates do not create tick logs")
        power.receiveBattery(battery(65, plugged: true, charging: true))
        check(powerLog.events.reversed().map(\.kind) == [.powerConnected, .batteryStateChanged],
              "A confirmed cable connection and charging-state change are recorded once")
        check(powerLog.events.allSatisfy { $0.metadata == ["state": "charging", "percentage": "65"] },
              "Power transitions contain only the new state and current percentage")
        power.receiveBattery(battery(66, plugged: true, charging: true))
        check(powerLog.events.count == 2, "A charging percentage tick does not duplicate connection or state events")
        power.receiveBattery(battery(80, plugged: true))
        check(powerLog.events.first?.kind == .batteryStateChanged && powerLog.events.first?.metadata["state"] == "connected"
              && powerLog.events.count == 3,
              "Paused charging on a connected cable records only the battery-state transition")
        power.receiveBattery(battery(100, plugged: true, full: true))
        check(powerLog.events.first?.metadata["state"] == "full" && powerLog.events.count == 4,
              "A fully charged battery has its own state event")
        power.receiveBattery(.unavailable)
        check(powerLog.events.first?.metadata == ["state": "unavailable"]
              && !powerLog.events.contains { $0.kind == .powerDisconnected },
              "Missing battery data records unavailability without inventing an unplug event")
        power.receiveBattery(.unavailable)
        power.receiveBattery(battery(99, plugged: true, full: true))
        check(powerLog.events.filter { $0.kind == .powerConnected }.count == 1 && powerLog.events.count == 6,
              "Recovery from unavailable battery data does not invent a second cable connection")
        power.receiveBattery(battery(99))
        check(Array(powerLog.events.prefix(2)).map(\.kind) == [.batteryStateChanged, .powerDisconnected]
              && powerLog.events.first?.metadata["state"] == "battery",
              "A confirmed unplug records disconnection and the transition to battery power")
        let desktopLog = SystemEventLog()
        let desktop = SystemEventRecorder(log: desktopLog)
        desktop.receiveBattery(.unavailable); desktop.receiveBattery(.unavailable)
        check(desktopLog.events.isEmpty, "A desktop without a battery remains quiet at startup and on repeated refreshes")

        let workLog = SystemEventLog()
        let recorder = SystemEventRecorder(log: workLog)
        var now: TimeInterval = 0
        let controller = WorkModeController(clock: { now }, scheduleTimer: { _, _, _, _ in QuietTimer() })
        recorder.receiveWork(controller.snapshot)
        let token = controller.observe { recorder.receiveWork(controller.snapshot) }
        defer { controller.removeObserver(token); controller.shutdown() }
        _ = controller.chooseCountdown(seconds: 300)
        controller.setVisible(true)
        check(workLog.events.isEmpty, "Initial timer state, idle preset changes and visibility changes are quiet")
        check(!controller.chooseCountdown(seconds: .nan) && workLog.events.isEmpty,
              "A rejected timer edit does not claim a successful work event")
        controller.start(); controller.start()
        check(workLog.events.map(\.kind) == [.workStarted]
              && workLog.events.first?.metadata == ["kind": "countdown", "seconds": "300"],
              "A successful countdown start logs its duration once despite repeated Start commands")
        now = 20; controller.refresh()
        controller.setVisible(false); controller.setVisible(true)
        check(workLog.events.count == 1, "Elapsed ticks and opening or closing the HUD do not create work events")
        controller.pause(); controller.pause()
        check(workLog.events.first?.kind == .workPaused && workLog.events.count == 2,
              "A successful pause logs once; a redundant pause is quiet")
        controller.resume(); controller.resume()
        check(workLog.events.first?.kind == .workResumed && workLog.events.count == 3,
              "Resume is distinguished from starting a new work session")
        now += 400; controller.refresh(); controller.refresh()
        check(workLog.events.first?.kind == .workCompleted && workLog.events.count == 4,
              "A countdown completion is logged once even after a delayed wake or repeated refresh")
        controller.reset(); controller.reset()
        check(workLog.events.first?.kind == .workReset && workLog.events.count == 5,
              "Resetting a completed countdown logs once without an idle reset tick")
        controller.start(); controller.chooseStopwatch()
        check(workLog.events.first?.kind == .workReset
              && workLog.events.first?.metadata == ["kind": "countdown", "seconds": "300"],
              "Changing mode resets the previous active session and preserves its correct metadata")
        controller.start()
        check(workLog.events.first?.kind == .workStarted && workLog.events.first?.metadata == ["kind": "stopwatch"],
              "A stopwatch start never claims a countdown duration")
        let beforeTicks = workLog.events.count
        for _ in 0..<20 { now += 1; controller.refresh() }
        check(workLog.events.count == beforeTicks, "Visible stopwatch ticks do not fill the event log")
        controller.pause(); controller.reset()
        check(workLog.events.first?.kind == .workReset && workLog.events.first?.metadata == ["kind": "stopwatch"],
              "Reset from paused stopwatch records the completed action with the old mode")
        let malformedLog = SystemEventLog()
        let malformed = SystemEventRecorder(log: malformedLog)
        malformed.receiveWork(WorkModeSnapshot(kind: .countdown, phase: .idle, duration: 300, elapsed: 0))
        malformed.receiveWork(WorkModeSnapshot(kind: .countdown, phase: .running, duration: .infinity, elapsed: 0))
        check(malformedLog.events.first?.metadata == ["kind": "countdown"],
              "An invalid diagnostic duration cannot crash metadata conversion or persist nonfinite seconds")

        let settingsLog = SystemEventLog(), settings = SystemEventRecorder(log: SystemEventLog())
        let settingsRecorder = SystemEventRecorder(log: settingsLog)
        var configuration = AppConfiguration.defaults
        configuration.clockStyle = .dial; configuration.alertMetric = .ram
        settingsRecorder.receiveConfiguration(configuration)
        settingsRecorder.receiveConfiguration(configuration)
        check(settingsLog.events.isEmpty, "Saved display settings establish a quiet startup baseline")
        configuration.hudDisplayName = "PRIVATE DISPLAY NAME"
        configuration.displayDuration = 11
        settingsRecorder.receiveConfiguration(configuration)
        check(settingsLog.events.isEmpty, "Unrelated preferences and user-entered display names are never recorded")
        configuration.clockStyle = .rail
        settingsRecorder.receiveConfiguration(configuration); settingsRecorder.receiveConfiguration(configuration)
        check(settingsLog.events.count == 1 && settingsLog.events[0].metadata == ["field": "clockStyle", "value": "rail"],
              "A changed clock style records once despite repeated configuration publications")
        let privateRevision = UUID().uuidString
        configuration.centerLogo = .custom; configuration.centerLogoRevision = privateRevision
        settingsRecorder.receiveConfiguration(configuration)
        check(settingsLog.events.count == 2 && settingsLog.events[0].metadata == ["field": "centerLogo", "value": "customImported"],
              "Selecting and importing custom artwork in one commit records one import without its revision ID")
        configuration.centerLogoRevision = UUID().uuidString
        settingsRecorder.receiveConfiguration(configuration)
        check(settingsLog.events.count == 3 && settingsLog.events[0].detail == "Center logo · Custom artwork imported",
              "Replacing an already selected custom logo records the committed import")
        configuration.centerLogo = .babel
        settingsRecorder.receiveConfiguration(configuration)
        configuration.centerLogoRevision = UUID().uuidString
        settingsRecorder.receiveConfiguration(configuration)
        check(settingsLog.events.count == 4 && settingsLog.events[0].metadata == ["field": "centerLogo", "value": "babel"],
              "Preset changes record once; an inactive custom-image revision creates no visible-change event")
        configuration.alertMetric = .network
        settingsRecorder.receiveConfiguration(configuration); settingsRecorder.receiveConfiguration(configuration)
        check(settingsLog.events.count == 5 && settingsLog.events[0].metadata == ["field": "alertMetric", "value": "network"],
              "A charge-metric selection records its enum value once")
        check(settingsLog.events.allSatisfy { !$0.metadata.values.contains(privateRevision)
            && !$0.metadata.values.contains("PRIVATE DISPLAY NAME") && $0.metadata.keys.sorted() == ["field", "value"] },
              "Settings history never retains artwork IDs, user profile content or unrelated fields")
        settings.receiveProfileCrop(backgroundZoom: 1, thumbnailZoom: 1)
        settings.receiveProfileCrop(backgroundZoom: .nan, thumbnailZoom: .infinity)
        check(settings.log.events.isEmpty, "Initial crop state and equivalent invalid-value fallbacks remain quiet")
        settings.receiveProfileCrop(backgroundZoom: 2.75, thumbnailZoom: 1)
        settings.receiveProfileCrop(backgroundZoom: 2.75, thumbnailZoom: 1)
        check(settings.log.events.count == 1 && settings.log.events[0].metadata == ["target": "background"],
              "A committed background zoom change records once without its numeric crop")
        settings.receiveProfileCrop(backgroundZoom: 2.75, thumbnailZoom: 4.25)
        check(settings.log.events.count == 2 && settings.log.events[0].metadata == ["target": "thumbnail"],
              "Thumbnail zoom changes retain their independent target")
        settings.receiveProfileCrop(backgroundZoom: 1, thumbnailZoom: 1)
        check(settings.log.events.count == 3 && settings.log.events[0].metadata == ["target": "both"],
              "One successful commit changing both crops creates one event")

        let suite = "EndfieldHUD-EventSettingsTests-\(UUID().uuidString)"
        let previousLanguage = L10n.language
        let defaults = UserDefaults(suiteName: suite)!
        defer { defaults.removePersistentDomain(forName: suite); L10n.language = previousLanguage }
        let store = ConfigurationStore(defaults: defaults), committedLog = SystemEventLog()
        let committed = SystemEventRecorder(log: committedLog)
        committed.receiveConfiguration(store.configuration)
        let settingsToken = store.addObserver { committed.receiveConfiguration($0) }
        defer { store.removeObserver(settingsToken) }
        var discardedDraft = store.configuration
        discardedDraft.clockStyle = .stacked
        check(discardedDraft.clockStyle == .stacked && committedLog.events.isEmpty, "An uncommitted settings draft creates no history")
        store.update(discardedDraft)
        store.update(discardedDraft)
        check(committedLog.events.count == 1 && defaults.string(forKey: "clockStyle") == "stacked",
              "Real ConfigurationStore commit publication logs once and no-op saves remain silent")

        let topologyLog = SystemEventLog()
        let topology = SystemEventRecorder(log: topologyLog)
        topology.receiveAudioDevices(["private-uid-a": "Built-in Speakers"])
        topology.receiveDisplays(["display-id-a": "Built-in Display"])
        check(topologyLog.events.isEmpty, "Initial audio and display inventories do not fabricate connection events")
        topology.receiveAudioDevices(["private-uid-a": "Renamed Speakers"])
        topology.receiveDisplays(["display-id-a": "Renamed Display"])
        check(topologyLog.events.isEmpty, "Renaming a device preserves its identity and creates no reconnect pair")
        topology.receiveAudioDevices(["private-uid-a": "Renamed Speakers", "private-uid-b": "USB Headphones"])
        topology.receiveDisplays(["display-id-a": "Renamed Display", "display-id-b": "Studio Display"])
        check(topologyLog.events.map(\.kind) == [.displayConnected, .audioDeviceConnected],
              "New physical audio and display identities produce the corresponding connection events")
        topology.receiveAudioDevices(["private-uid-a": "Renamed Speakers"])
        topology.receiveDisplays(["display-id-a": "Renamed Display"])
        check(Array(topologyLog.events.prefix(2)).map(\.kind) == [.displayDisconnected, .audioDeviceDisconnected],
              "Removing a known device records one disconnection in its own category")
        topology.receiveAudioDevices(["private-uid-a": "Renamed Speakers", "private-uid-b": "USB Headphones"])
        check(topologyLog.events.first?.kind == .audioDeviceConnected && topologyLog.events.first?.metadata == ["device": "USB Headphones"],
              "A real reconnect uses the human label and records a fresh connection")
        let beforeIdentical = topologyLog.events.count
        topology.receiveAudioDevices(["private-uid-b": "USB Headphones", "private-uid-a": "Renamed Speakers"])
        check(topologyLog.events.count == beforeIdentical, "Dictionary ordering changes cannot create topology events")
        topology.receiveAudioDevices([:])
        check(Array(topologyLog.events.prefix(2)).allSatisfy { $0.kind == .audioDeviceDisconnected }
              && topologyLog.events.first?.metadata["device"] == "USB Headphones",
              "A confirmed empty inventory disconnects prior devices in deterministic identity order")
        check(topologyLog.events.allSatisfy { $0.metadata.keys.allSatisfy { $0 == "device" }
            && !$0.metadata.values.contains(where: { $0.contains("private-uid") || $0.contains("display-id") }) },
              "Hardware UIDs and display identifiers remain in memory and never enter persisted event metadata")
        return count
    }

    private final class QuietTimer: WorkModeTimer { func invalidate() {} }
}
