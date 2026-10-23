import AppKit
import QuartzCore

// Actual-source oracle of Settings (Mac 1.3.0 Sources/HUDSettingsController.swift and
// Sources/HUDSettingsCanvas.swift, with ConfigurationStore on temporary defaults):
//  - the controller's preference transactions at fixed checkpoints (`controller`, unchanged since
//    v1.2.0: tests/settings_tests.cpp);
//  - the four redesigned canvases (System, Display, Hotkeys, About) built by the unchanged Mac code in
//    scripted states (`pages`: sub-pages, menus, dark / Light, English / Simplified Chinese, a module
//    layout, alerts switched off, shortcut recording, the update strip) exported as their model layer trees
//    with the design recorder (tools/hud_design_reference_support.swift), page controls and
//    accessibility items, plus the scenario's configuration;
//  - the confirm requests and toasts the canvases raise (`requests`), the Center logo rasters' pixel sizes
//    (`logos`), the About information (`about`) and the recorded text widths and line metrics.
// Isolation: temporary CFFIXED_USER_HOME and a volatile defaults suite; a fixed display list; a fake clock
// and timer; no window, no input posted; Reduce Motion on, so every page installs at once.

private final class SettingsFixtureTimer: HUDSettingsTimer { func invalidate() {} }
private var reduce = true
private let env = HUDDesignEnvironment(renderScale: 2, reduceMotion: { reduce })

private func configurationRow(_ c: AppConfiguration) -> [String: Any] {
    ["language": c.language.rawValue, "theme": c.theme.rawValue, "accentHex": c.accentHex, "launchAtLogin": c.launchAtLogin,
     "closeOnFocusLost": c.closeOnFocusLost, "openOnActiveDisplay": c.openOnActiveDisplay, "hudScale": c.hudScale,
     "hudOffsetX": c.hudOffsetX, "hudOffsetY": c.hudOffsetY, "parallaxIntensity": c.parallaxIntensity,
     "perspectiveIntensity": c.perspectiveIntensity, "backgroundDarkness": c.backgroundDarkness, "blurAmount": c.blurAmount,
     "clockFormat": c.clockFormat.rawValue, "clockStyle": c.clockStyle.rawValue, "centerLogo": c.centerLogo.rawValue,
     "applicationIcon": c.applicationIcon.rawValue, "alertMetric": c.alertMetric.rawValue, "displayMode": c.displayMode.rawValue,
     "batteryAlertsEnabled": c.batteryAlertsEnabled, "displayDuration": c.displayDuration, "placement": c.placement.rawValue,
     "scale": c.scale, "ambientAnimation": c.ambientAnimation, "reduceMotion": c.reduceMotion,
     "lowPowerVisualMode": c.lowPowerVisualMode, "recordingMode": c.recordingMode, "dailyBootLog": c.dailyBootLog,
     "facilityAlerts": c.facilityAlerts, "summonHotkeyEnabled": c.summonHotkeyEnabled,
     "hudModuleOrder": c.moduleLayout.order.map(\.rawValue), "hudHiddenModules": c.moduleLayout.order.filter { c.moduleLayout.isHidden($0) }.map(\.rawValue)]
}
private func request(_ r: HUDConfirmRequest?) -> Any {
    guard let r else { return NSNull() }
    return ["id": r.id, "title": r.title, "message": r.message, "countdown": r.countdown as Any? ?? NSNull(), "confirmTitle": r.confirmTitle,
            "cancelTitle": r.cancelTitle, "danger": r.danger, "icon": r.glyph.rawValue, "alert": r.style == .alert, "cancelPillConfirms": r.cancelPillConfirms]
}
private func toast(_ t: HUDToastRequest?) -> Any {
    guard let t else { return NSNull() }
    return ["icon": t.icon.rawValue, "title": t.title, "caption": t.caption]
}

@main private enum SettingsReference {
    static func main() throws {
        precondition(ProcessInfo.processInfo.environment["CFFIXED_USER_HOME"] != nil)
        precondition(CommandLine.arguments.count >= 2)
        let output = URL(fileURLWithPath: CommandLine.arguments[1])
        NSApplication.shared.setActivationPolicy(.prohibited)
        let suite = "EndfieldSettingsReference." + UUID().uuidString
        let defaults = UserDefaults(suiteName: suite)!
        defaults.setVolatileDomain([:], forName: suite)
        defer { defaults.removePersistentDomain(forName: suite); defaults.removeVolatileDomain(forName: suite) }
        var initial = AppConfiguration.defaults
        initial.language = .english; initial.theme = .dark; initial.launchAtLogin = false; initial.reduceMotion = true
        HUDRuntimeAppearance.configuration = initial; L10n.language = .english
        HUDColor.apply(accentHex: "FAD41F", light: false)
        let store = ConfigurationStore(defaults: defaults); store.update(initial)
        var now = 0.0
        let controller = HUDSettingsController(store: store, clock: { now }, scheduleTimer: { _, _ in SettingsFixtureTimer() })
        controller.onLaunchAtLoginChange = { _ in "Synthetic login failure" }
        controller.onShortcutChange = { _ in nil }

        // MARK: Controller checkpoints (unchanged sequence)
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
        now = 100

        // MARK: Canvases
        let recorder = try Recorder(output: output)
        let display = HUDDisplayDescriptor(uuid: "00000000-0000-4000-8000-000000000010", name: "Fixture Display", displayID: 1,
            frame: CGRect(x: 0, y: 0, width: 1920, height: 1080))
        let context = HUDDesignContext(env: env)
        let style = HUDModuleContentStyle(dark: true, accent: .yellow, contentsScale: 2)
        var update = HUDUpdateState(); update.automaticallyInstalls = true
        controller.receiveUpdateState(update)
        var canvases: [HUDModule: HUDSettingsCanvas] = [:]
        for module in [HUDModule.system, .display, .hotkeys, .about] {
            canvases[module] = HUDSettingsCanvas(module: module, controller: controller, context: context, displayProvider: { [display] })
        }
        var pages: [[String: Any]] = [], requests: [[String: Any]] = []
        var language = AppLanguage.english, light = false
        func row(_ module: HUDModule, _ state: String, extra: [String: Any] = [:]) throws {
            let canvas = canvases[module]!
            var r: [String: Any] = ["module": module.rawValue, "state": state, "language": languageName(language), "light": light,
                "layer": try recorder.tree(canvas.root, id: "root"),
                "controls": canvas.page.controls.map { controlState($0, in: canvas.root) },
                "accessibility": accessibility(canvas.accessibilityItems),
                "header": ["counter": canvas.header.counterTextForTesting, "title": canvas.header.titleTextForTesting.0,
                           "subtitle": canvas.header.titleTextForTesting.1, "crumbs": canvas.header.crumbTextsForTesting],
                "configuration": configurationRow(controller.configuration), "menuOpen": canvas.isMenuOpen,
                "update": ["phase": "\(controller.updateState.phase)", "latestVersion": controller.updateState.latestVersion as Any? ?? NSNull(),
                           "automaticallyInstalls": controller.updateState.automaticallyInstalls,
                           "releaseURL": controller.updateState.releaseURL?.absoluteString as Any? ?? NSNull(),
                           "title": controller.updateState.title, "detail": controller.updateState.detail as Any? ?? NSNull()]]
            for (k, v) in extra { r[k] = v }
            pages.append(r)
        }
        func ask(_ name: String, _ module: HUDModule, extra: [String: Any] = [:]) {
            var r: [String: Any] = ["name": name, "module": module.rawValue, "language": languageName(language),
                                    "request": request(context.overlay.confirmState.request), "toast": toast(context.overlay.toastState.current)]
            for (k, v) in extra { r[k] = v }
            requests.append(r)
        }
        func show(_ module: HUDModule) {
            let canvas = canvases[module]!
            _ = canvas.makeContent(for: module, style: style); canvas.setActive(true)
        }
        func hide(_ module: HUDModule) { canvases[module]!.setActive(false) }
        func control(_ module: HUDModule, _ id: String) -> HUDControl {
            guard let found = canvases[module]!.page.controls.first(where: { $0.id == id }) else { preconditionFailure("No control \(id)") }
            return found
        }
        func clear() { context.overlay.dismissConfirm(); context.overlay.clearToast() }

        // System
        show(.system)
        try row(.system, "main")
        control(.system, "language").activate(); try row(.system, "menu.language"); canvases[.system]!.closeMenu()
        control(.system, "screen").activate(); try row(.system, "menu.screen"); canvases[.system]!.closeMenu()
        control(.system, "restore").activate(); ask("restore", .system); clear()
        controller.setStatus("Synthetic status"); ask("notice", .system); controller.setStatus(nil); clear()
        control(.system, "modules").activate(); try row(.system, "modules")
        controller.update { $0.moduleLayout.move(.clipboard, to: 0); _ = $0.moduleLayout.setHidden(.archive, true) }
        try row(.system, "modules.changed")
        control(.system, "modulesReset").activate(); ask("modulesReset", .system); clear()
        controller.update { $0.moduleLayout = HUDModuleLayout() }
        canvases[.system]!.page.back?()
        try row(.system, "main.back")
        hide(.system)
        L10n.language = .simplifiedChinese; language = .simplifiedChinese
        show(.system); try row(.system, "main"); hide(.system)
        L10n.language = .english; language = .english
        light = true; HUDColor.apply(accentHex: "FAD41F", light: true); context.refreshPalette()
        show(.system); try row(.system, "main"); hide(.system)
        light = false; HUDColor.apply(accentHex: "FAD41F", light: false); context.refreshPalette()

        // Display
        show(.display)
        try row(.display, "main")
        _ = canvases[.display]!.scroll(at: CGPoint(x: 300, y: 300), delta: CGPoint(x: 0, y: 10000))
        try row(.display, "main.bottom")
        control(.display, "facility").activate(); ask("facilityOff", .display, extra: ["toggleOn": (control(.display, "facility") as! HUDToggle).isOn]); clear()
        control(.display, "recording").activate(); ask("recordingOn", .display)
        control(.display, "recording").activate(); ask("recordingOff", .display); clear()
        _ = canvases[.display]!.scroll(at: CGPoint(x: 300, y: 300), delta: CGPoint(x: 0, y: -10000))
        for menu in ["theme", "clockStyle", "centerLogo"] {
            control(.display, menu).activate(); try row(.display, "menu." + menu); canvases[.display]!.closeMenu()
        }
        controller.previewScale(1.5); ask("layout", .display)
        now += 13; controller.checkLayoutTimeout(); ask("layoutReverted", .display); clear()
        controller.previewPosition(x: 0.2, y: 0); ask("position", .display)
        context.overlay.decide(.cancel); clear()
        control(.display, "battery").activate(); try row(.display, "battery")
        control(.display, "method").activate(); try row(.display, "menu.method"); canvases[.display]!.closeMenu()
        controller.update { $0.batteryAlertsEnabled = false }
        try row(.display, "battery.alertsOff")
        controller.update { $0.batteryAlertsEnabled = true }
        canvases[.display]!.page.back?()
        control(.display, "appIcon").activate(); try row(.display, "icons")
        canvases[.display]!.page.back?()
        hide(.display)
        light = true; HUDColor.apply(accentHex: "FAD41F", light: true); context.refreshPalette()
        show(.display); try row(.display, "main"); hide(.display)
        light = false; HUDColor.apply(accentHex: "FAD41F", light: false); context.refreshPalette()
        L10n.language = .simplifiedChinese; language = .simplifiedChinese
        show(.display); try row(.display, "main"); hide(.display)
        L10n.language = .english; language = .english

        // Hotkeys
        show(.hotkeys)
        try row(.hotkeys, "main")
        control(.hotkeys, "capture").activate(); try row(.hotkeys, "capture")
        canvases[.hotkeys]!.cancelCapture()
        hide(.hotkeys)

        // About
        show(.about)
        try row(.about, "main")
        var current = HUDUpdateState(); current.phase = .current; current.latestVersion = "1.3.0"
        current.releaseURL = URL(string: "https://github.com/DDDuoDuo/EndfieldHUD/releases/tag/v1.3.0")
        controller.receiveUpdateState(current)
        try row(.about, "main.current")
        var failed = current; failed.phase = .failed; failed.detail = "Synthetic network failure"
        controller.receiveUpdateState(failed); ask("updateFailed", .about); clear()
        try row(.about, "main.failed")
        controller.receiveUpdateState(update)
        control(.about, "credits").activate(); try row(.about, "credits")
        hide(.about)

        // The Center logo rasters (HUDSettingsCanvas.logoArtwork: the presented logo aspect-fit at scale 2).
        var logos: [[String: Any]] = []
        func wordmark() -> CGImage? {
            guard let url = HUDResources.url(for: "EndfieldIndustriesSource.png"),
                  let source = CGImageSourceCreateWithURL(url as CFURL, nil),
                  let image = CGImageSourceCreateImageAtIndex(source, 0, nil) else { return nil }
            let unit = CGFloat(image.width) / 430
            return image.cropping(to: CGRect(x: 0, y: 142 * unit, width: CGFloat(image.width), height: 83 * unit).integral)
        }
        for logo in [HUDCenterLogo.endfield, .rhodesIsland, .babel, .rhineLab] {
            guard let source = logo == .endfield ? wordmark() : logo.image(revision: nil),
                  let presented = HUDCenterLogoPresentation.image(source) else { continue }
            for size in [CGSize(width: 130, height: 16), CGSize(width: 104, height: 28)] {
                guard let raster = HUDMenu.Icon.raster(presented, fitting: size, scale: 2) else { continue }
                logos.append(["logo": logo.rawValue, "fitting": [size.width, size.height], "pixels": [raster.width, raster.height],
                              "presented": [presented.width, presented.height]])
            }
        }
        let about = HUDAboutInformation.current()
        let payload: [String: Any] = ["schema": 2, "pages": pages, "requests": requests, "logos": logos,
            "about": ["name": about.name, "version": about.version, "author": about.author, "license": about.licenseName,
                      "repository": about.repositoryURL?.absoluteString as Any? ?? NSNull(),
                      "credits": about.credits.map { ["name": $0.name, "role": $0.role, "url": $0.url?.absoluteString as Any? ?? NSNull()] }],
            "icons": HUDApplicationIcon.pickerCases.map { icon -> [String: Any] in
                L10n.language = .simplifiedChinese; defer { L10n.language = .english }
                let chinese = icon.title
                L10n.language = .english
                return ["id": icon.rawValue, "title": icon.title, "titleZh": chinese]
            },
            "text": ["widths": Array(recorder.widths.values), "metrics": Array(recorder.metrics.values)],
            "layerDefaults": recorder.layerDefaults,
            "isolation": ["actualSources": true, "temporaryDefaults": true, "fakeDisplays": true, "fakeClockAndTimer": true,
                          "windowCreated": false, "inputPosted": false]]
        precondition(NSApp.windows.isEmpty)
        // The controller checkpoints keep full double precision (exact values the Windows controller reproduces).
        var exported = sanitize(payload) as! [String: Any]
        exported["controller"] = sanitize(states, exact: true)
        let data = try JSONSerialization.data(withJSONObject: exported, options: [.sortedKeys])
        try data.write(to: output.appendingPathComponent("settings-source.json"))
        print("Settings: \(states.count) controller checkpoints, \(pages.count) pages, \(requests.count) requests")
    }
}
