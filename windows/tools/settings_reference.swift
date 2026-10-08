import AppKit
import QuartzCore

private final class SettingsFixtureTimer: HUDSettingsTimer { func invalidate() {} }
@main private enum SettingsReference {
    static func main() throws {
        precondition(ProcessInfo.processInfo.environment["CFFIXED_USER_HOME"] != nil)
        precondition(CommandLine.arguments.count == 3 && CommandLine.arguments[1] == "--ui-test")
        let output = URL(fileURLWithPath: CommandLine.arguments[2]), encoder = try ModuleReferenceLayerEncoder(output: output)
        NSApplication.shared.setActivationPolicy(.prohibited)
        let suite = "EndfieldSettingsReference." + UUID().uuidString
        let defaults = UserDefaults(suiteName: suite)!
        defaults.setVolatileDomain([:], forName: suite)
        defer { defaults.removePersistentDomain(forName: suite); defaults.removeVolatileDomain(forName: suite) }
        var initial = AppConfiguration.defaults
        initial.language = .english; initial.theme = .dark; initial.launchAtLogin = false; initial.reduceMotion = true
        HUDRuntimeAppearance.configuration = initial; L10n.language = .english
        let store = ConfigurationStore(defaults: defaults); store.update(initial)
        var now = 0.0
        let controller = HUDSettingsController(store: store, clock: { now }, scheduleTimer: { _, _ in SettingsFixtureTimer() })
        controller.onLaunchAtLoginChange = { _ in "Synthetic login failure" }
        controller.onShortcutChange = { _ in nil }
        var states: [[String: Any]] = []
        func configuration(_ value: AppConfiguration) -> [String: Any] {
            ["hudScale": value.hudScale, "hudOffsetX": value.hudOffsetX, "hudOffsetY": value.hudOffsetY,
             "theme": value.theme.rawValue, "accentHex": value.accentHex, "launchAtLogin": value.launchAtLogin,
             "ambientAnimation": value.ambientAnimation]
        }
        func record(_ name: String) {
            states.append(["name": name, "time": now, "current": configuration(controller.configuration), "committed": configuration(store.configuration),
                           "remaining": controller.layoutConfirmationRemaining as Any? ?? NSNull(), "scalePending": controller.isScalePreviewPending,
                           "positionPending": controller.isPositionPreviewPending, "status": controller.status as Any? ?? NSNull(), "capturing": controller.isCapturingShortcut])
        }
        record("initial"); controller.previewScale(1.35); record("scale")
        controller.update { $0.theme = .light }; record("themeDuringPreview")
        now = 1; controller.previewPosition(x: -0.17, y: 0.1); record("position")
        now = 2.1; controller.checkLayoutTimeout(); record("tick")
        now = 12.9; controller.confirmLayout(); record("confirm")
        controller.previewScale(9); record("clamped"); now = 24.9; controller.confirmLayout(); record("expiredConfirm")
        now = 30; controller.previewPosition(x: 0.3, y: -0.2); controller.close(); record("closed")
        controller.update { $0.launchAtLogin = true; $0.ambientAnimation = false }; record("platformFailure")
        controller.beginShortcutCapture(); record("capture"); controller.close(); record("captureClosed")
        store.update(initial); controller.close(); controller.setStatus(nil)
        let display = HUDDisplayDescriptor(uuid: "00000000-0000-4000-8000-000000000010", name: "Fixture Display", displayID: 1,
            frame: CGRect(x: 0, y: 0, width: 1920, height: 1080))
        var artwork: [[String: Any]] = []
        func emit(_ canvas: HUDSettingsCanvas, _ name: String) throws {
            artwork.append(["module": canvas.module.rawValue, "name": name, "layer": try encoder.encode(canvas.layer, id: "settings"),
                            "actions": canvas.accessibleActions.map { ["id": $0.id, "label": $0.label, "rect": ModuleReferenceLayerEncoder.rect($0.rect), "enabled": $0.enabled] },
                            "sliders": canvas.accessibleSliders.map { ["id": $0.id, "label": $0.label, "rect": ModuleReferenceLayerEncoder.rect($0.rect), "value": $0.value, "minimum": $0.minimum, "maximum": $0.maximum, "formatted": $0.valueDescription] },
                            "scroll": canvas.scrollOffset, "status": canvas.accessibilityStatus])
        }
        func find(_ id: String, _ canvas: HUDSettingsCanvas) {
            _ = canvas.scroll(at: CGPoint(x: 200, y: 100), delta: -10000)
            for _ in 0..<20 {
                if canvas.accessibleActions.contains(where: { $0.id == id }) { canvas.perform(actionID: id); return }
                _ = canvas.scroll(at: CGPoint(x: 200, y: 100), delta: 100)
            }
            preconditionFailure("Missing actual source action \(id)")
        }
        for module in [HUDModule.system, .display, .hotkeys, .about] {
            let canvas = HUDSettingsCanvas(module: module, controller: controller, reduceMotion: { true }, displayProvider: { [display] })
            _ = canvas.makeContent(for: module, style: .init(dark: true, accent: initial.accentColor, contentsScale: 2)); canvas.activate()
            try emit(canvas, "main"); _ = canvas.scroll(at: CGPoint(x: 200, y: 100), delta: 10000); try emit(canvas, "bottom")
            if module == .system {
                for action in ["language", "screen", "restore"] { find(action, canvas); try emit(canvas, action); _ = canvas.escape() }
            } else if module == .display {
                for action in ["battery", "centerLogo", "appIcon"] {
                    find(action, canvas); try emit(canvas, action)
                    if action == "battery" { find("metric", canvas); try emit(canvas, "metric"); _ = canvas.escape() }
                    if action == "appIcon" { _ = canvas.scroll(at: CGPoint(x: 200, y: 100), delta: 10000); try emit(canvas, "iconsBottom") }
                    _ = canvas.escape()
                }
                _ = canvas.scroll(at: CGPoint(x: 200, y: 100), delta: -10000)
                _ = canvas.makeContent(for: module, style: .init(dark: false, accent: initial.accentColor, contentsScale: 2)); try emit(canvas, "light")
                _ = canvas.mouseDown(at: CGPoint(x: 300, y: 55)); canvas.mouseDragged(to: CGPoint(x: 350, y: 55)); try emit(canvas, "stagedScale"); canvas.mouseUp(); controller.revertLayout()
            } else if module == .hotkeys { find("capture", canvas); try emit(canvas, "capture"); canvas.cancelCapture() }
            canvas.deactivate()
        }
        precondition(NSApp.windows.isEmpty)
        let payload: [String: Any] = ["schema": 1, "controller": states, "artwork": artwork, "rasters": encoder.rasterAssets, "unsupported": encoder.unsupported,
            "isolation": ["actualSources": true, "temporaryDefaults": true, "fakeDisplays": true, "fakeClockAndTimer": true, "windowCreated": false, "liveProviders": false]]
        try JSONSerialization.data(withJSONObject: payload, options: [.sortedKeys]).write(to: output.appendingPathComponent("reference.json"))
        print("Settings original source: \(states.count) controller checkpoints, \(artwork.count) detached artwork states")
    }
}
