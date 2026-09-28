import AppKit
import QuartzCore

enum HUDCanvasLifecycleTests {
    private final class TestTimer: WorkModeTimer, HUDSettingsTimer {
        let repeats: Bool
        var invalidated = false
        init(repeats: Bool = false) { self.repeats = repeats }
        func invalidate() { invalidated = true }
    }
    static func run() -> Int {
        var count = 0
        func check(_ value: Bool, _ description: String) {
            count += 1
            if !value { fatalError(description) }
        }
        func animations(_ layer: CALayer) -> Int {
            (layer.animationKeys()?.count ?? 0) + (layer.sublayers ?? []).reduce(0) { $0 + animations($1) }
                + (layer.mask.map(animations) ?? 0)
        }
        let style = HUDModuleContentStyle(dark: true, accent: .systemYellow, contentsScale: 2)
        var now: TimeInterval = 100
        var timers: [TestTimer] = []
        let work = WorkModeController(clock: { now }, scheduleTimer: { _, repeats, _, _ in
            let timer = TestTimer(repeats: repeats); timers.append(timer); return timer
        })
        work.chooseStopwatch()
        var canvas: WorkModeCanvas? = WorkModeCanvas(controller: work, reduceMotion: { false })
        let retainedWorkLayer = canvas!.makeContent(for: .workMode, style: style)
        var workChanges = 0
        canvas!.onChange = { workChanges += 1 }
        canvas!.activate()
        let activatedChanges = workChanges
        canvas!.activate()
        check(workChanges == activatedChanges, "Repeated activation does not rebuild or notify an already visible Work Mode canvas")
        canvas!.perform(actionID: "work:start")
        check(timers.contains { $0.repeats && !$0.invalidated }, "A visible stopwatch owns a display ticker")
        weak var releasedWork = canvas
        canvas = nil
        check(releasedWork == nil && !timers.contains { !$0.invalidated }, "Destroying a stopwatch canvas ends the visible ticker even while its controller survives")
        check(animations(retainedWorkLayer) == 0, "Destroying a Work Mode canvas cancels its tracks even if a transition retains its layer")
        now += 12
        check(work.snapshot.phase == .running && work.snapshot.elapsed == 12, "Canvas teardown preserves the running Work Mode session")
        let hiddenWork = WorkModeCanvas(controller: work, reduceMotion: { true })
        var hiddenChanges = 0
        hiddenWork.onChange = { hiddenChanges += 1 }
        hiddenWork.updateRenderScale(4)
        check(hiddenChanges == 0 && !timers.contains { !$0.invalidated }, "Hidden scale updates neither rebuild Work Mode nor start a display timer")
        _ = hiddenWork.makeContent(for: .workMode, style: style)
        hiddenWork.activate(); hiddenWork.deactivate(); work.shutdown()

        let audio = AudioDeviceController(snapshot: AudioDeviceSnapshot())
        var volume: VolumeCanvas? = VolumeCanvas(controller: audio)
        let retainedVolumeLayer = volume!.makeContent(for: .volume, style: style)
        volume!.activate()
        check(audio.isRunning, "Volume activation owns device observation")
        let audioBefore = audio.snapshot
        weak var releasedVolume = volume
        volume = nil
        check(releasedVolume == nil && !audio.isRunning && audio.snapshot == audioBefore,
              "Volume teardown releases device observation without changing the hardware snapshot")
        check(animations(retainedVolumeLayer) == 0, "A retained Volume layer has no canvas action animations after owner teardown")

        let monitor = SystemActivityMonitor.fixture()
        let apps = AppActivityMonitor.fixture()
        let activity = ActivityMonitorCanvas(controller: monitor, apps: apps, reduceMotion: { true })
        _ = activity.makeContent(for: .activityMonitor, style: style)
        activity.activate(); activity.perform(actionID: "activity:apps")
        var edgeChanges = 0
        activity.onChange = { edgeChanges += 1 }
        check(activity.scroll(at: CGPoint(x: 100, y: 150), delta: -1000), "The app-list viewport consumes a scroll at its upper edge")
        check(edgeChanges == 0, "Clamped app-list scrolling does not repaint or rebuild accessibility")
        _ = activity.scroll(at: CGPoint(x: 100, y: 150), delta: 0)
        check(edgeChanges == 0, "Zero-distance app-list scrolling does not invalidate retained content")
        activity.deactivate(); monitor.shutdown()

        let suite = "EndfieldHUD.CanvasLifecycle.\(UUID().uuidString)"
        let defaults = UserDefaults(suiteName: suite)!
        defer { defaults.removePersistentDomain(forName: suite) }
        let settings = HUDSettingsController(store: ConfigurationStore(defaults: defaults), scheduleTimer: { _, _ in TestTimer() })
        var captureStates: [Bool] = []
        settings.onShortcutCaptureChange = { captureStates.append($0) }
        var hotkeys: HUDSettingsCanvas? = HUDSettingsCanvas(module: .hotkeys, controller: settings)
        _ = hotkeys!.makeContent(for: .hotkeys, style: style)
        hotkeys!.activate(); hotkeys!.perform(actionID: "capture")
        check(settings.isCapturingShortcut, "The Hotkeys canvas can own a shortcut capture session")
        weak var releasedHotkeys = hotkeys
        hotkeys = nil
        check(releasedHotkeys == nil && !settings.isCapturingShortcut && captureStates == [true, false],
              "Destroying Hotkeys releases its capture so the global summon shortcut can resume")
        let closingHotkeys = HUDSettingsCanvas(module: .hotkeys, controller: settings)
        _ = closingHotkeys.makeContent(for: .hotkeys, style: style)
        closingHotkeys.activate(); closingHotkeys.perform(actionID: "capture")
        var hiddenSettingsChanges = 0
        closingHotkeys.onChange = { hiddenSettingsChanges += 1 }
        closingHotkeys.deactivate()
        check(!settings.isCapturingShortcut && hiddenSettingsChanges == 0,
              "Closing shortcut capture restores global input without rebuilding hidden settings artwork")
        return count
    }
}
