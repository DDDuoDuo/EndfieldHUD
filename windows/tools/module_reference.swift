import AppKit
import QuartzCore

/// Compiled alongside current Mac Sources. Only neutral fixtures are constructed;
/// no SystemHUDView, NSWindow, live provider, account, clipboard or app coordinator.
@main enum ModuleReferenceExporter {
    struct ProfileArchive: Codable { let version: Int; let profile: UserProfile }
    static func require(_ condition: @autoclosure () -> Bool, _ message: String) throws {
        if !condition() { throw HUDSourceError.invalid(message) }
    }
    static func json(_ value: Any, _ url: URL) throws {
        try JSONSerialization.data(withJSONObject: value, options: [.prettyPrinted, .sortedKeys, .withoutEscapingSlashes])
            .write(to: url, options: .atomic)
    }
    static func action(_ id: String, _ label: String, _ rect: CGRect, space: String = "module-local", enabled: Bool = true) -> [String: Any] {
        ["id": id, "label": label, "rect": ModuleReferenceLayerEncoder.rect(rect), "space": space, "enabled": enabled]
    }
    static func slider(_ id: String, _ label: String, _ rect: CGRect, _ value: Double, _ lower: Double, _ upper: Double, _ description: String) -> [String: Any] {
        ["id": id, "label": label, "rect": ModuleReferenceLayerEncoder.rect(rect), "space": "module-local",
         "value": value, "minimum": lower, "maximum": upper, "valueDescription": description]
    }
    static func projection(profile: UserProfile, size: CGSize) throws -> [String: Any] {
        let view = try HUDSourceWatchView(frame: CGRect(origin: .zero, size: size), desktopMode: true,
            desktopNavigationEntries: HUDDesktopWatchNavigation.entries(shortcuts: []))
        defer { view.conceal() }
        var captured: CATransform3D?
        view.onDesktopCenterPlane = { captured = $0 }
        view.pointerLocationProvider = { CGPoint(x: size.width / 2, y: size.height / 2) }
        view.isDesktopPointerLocked = { true }
        view.setDesktopProfile(profile, avatar: nil, background: nil)
        view.selectedDesktopModule = .profile
        view.showStable(); view.layoutSubtreeIfNeeded(); view.layout(); view.refreshPointerForVerification()
        guard let transform = captured else { throw HUDSourceError.invalid("Actual desktop center projection missing") }
        try require(view.window == nil && !view.hasDisplayTimerForVerification && view.sourceCursorSetCountForVerification == 0 && !view.backdropPreparingForVerification,
                    "Detached projection must remain timer/cursor/window/capture-free")
        let points = [CGPoint.zero, CGPoint(x: 500, y: 320), CGPoint(x: 1000, y: 640)]
        return ["viewport": [size.width, size.height], "desktopMode": true,
            "provider": "HUDSourceWatchView.onDesktopCenterPlane", "transform": ModuleReferenceLayerEncoder.transform(transform),
            "samplePoints": points.map { ["design": ModuleReferenceLayerEncoder.point($0),
                "screen": ModuleReferenceLayerEncoder.point(HUDMotionMath.project($0, through: transform))] },
            "windowCreated": false, "displayTimerActive": false, "cursorSetCount": 0]
    }
    static func serializerFixture(_ encoder: ModuleReferenceLayerEncoder, output: URL) throws {
        let root = CALayer(); root.name = "serializer.fixture"; root.frame = CGRect(x: 3, y: 4, width: 80, height: 60)
        root.opacity = 0.75; root.anchorPoint = CGPoint(x: 0.25, y: 0.75); root.masksToBounds = true
        root.transform = CATransform3DMakeTranslation(9, 12, 0)
        let shape = CAShapeLayer(); shape.name = "shape"; shape.frame = root.bounds; shape.fillRule = .evenOdd
        let path = CGMutablePath(); path.move(to: CGPoint(x: 1, y: 2)); path.addLine(to: CGPoint(x: 3, y: 4))
        path.addQuadCurve(to: CGPoint(x: 7, y: 8), control: CGPoint(x: 5, y: 6))
        path.addCurve(to: CGPoint(x: 13, y: 14), control1: CGPoint(x: 9, y: 10), control2: CGPoint(x: 11, y: 12)); path.closeSubpath()
        shape.path = path; shape.fillColor = NSColor.red.cgColor; shape.strokeColor = NSColor.blue.cgColor
        shape.lineDashPattern = [2, 3]; root.addSublayer(shape)
        let text = CATextLayer(); text.name = "text"; text.frame = CGRect(x: 0, y: 0, width: 60, height: 20)
        text.font = NSFont.systemFont(ofSize: 12); text.fontSize = 12
        text.string = NSAttributedString(string: "Neutral", attributes: [.font: NSFont.systemFont(ofSize: 12), .baselineOffset: 2, .foregroundColor: NSColor.white])
        root.addSublayer(text)
        let gradient = CAGradientLayer(); gradient.name = "gradient"; gradient.colors = [NSColor.black.cgColor, NSColor.white.cgColor]
        gradient.locations = [0, 1]; root.addSublayer(gradient)
        let mask = CAShapeLayer(); mask.path = CGPath(rect: root.bounds, transform: nil); root.mask = mask
        let pixels: [UInt8] = [255,0,0,255, 0,255,0,255, 0,0,255,255, 0,0,0,0]
        let provider = CGDataProvider(data: Data(pixels) as CFData)!
        let image = CGImage(width: 2, height: 2, bitsPerComponent: 8, bitsPerPixel: 32, bytesPerRow: 8,
            space: CGColorSpace(name: CGColorSpace.sRGB)!, bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedLast.rawValue),
            provider: provider, decode: nil, shouldInterpolate: true, intent: .defaultIntent)!
        let raster = CALayer(); raster.contents = image; raster.name = "raster"; root.addSublayer(raster)
        try json(encoder.encode(root, id: "fixture"), output.appendingPathComponent("serializer-fixture.json"))
    }
    static func run() throws {
        try require(Thread.isMainThread, "Main thread required")
        var args = Array(CommandLine.arguments.dropFirst()), output: URL?
        while !args.isEmpty {
            switch args.removeFirst() {
            case "--ui-test", "--render-module-reference": break
            case "--output": try require(!args.isEmpty, "Missing output directory"); output = URL(fileURLWithPath: args.removeFirst(), isDirectory: true)
            default: throw HUDSourceError.invalid("Unknown module reference argument")
            }
        }
        guard let output else { throw HUDSourceError.invalid("--output is required") }
        guard ProcessInfo.processInfo.environment["CFFIXED_USER_HOME"] != nil else { throw HUDSourceError.invalid("Use isolated module_reference.sh wrapper") }
        let dataRoot = FileManager.default.temporaryDirectory.appendingPathComponent("EndfieldModuleFixture-" + UUID().uuidString, isDirectory: true)
        try FileManager.default.createDirectory(at: dataRoot, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: dataRoot) }
        NSApplication.shared.setActivationPolicy(.prohibited); NSApp.appearance = NSAppearance(named: .darkAqua)
        NSTimeZone.default = TimeZone(secondsFromGMT: 0)!
        var config = AppConfiguration.defaults
        config.language = .english; config.theme = .dark; config.ambientAnimation = false; config.reduceMotion = true
        config.blurAmount = 0; config.hudScale = 1; config.hudOffsetX = 0; config.hudOffsetY = 0; config.launchAtLogin = false
        HUDRuntimeAppearance.configuration = config; L10n.language = .english
        let suite = "EndfieldModuleReference." + UUID().uuidString
        guard let defaults = UserDefaults(suiteName: suite) else { throw HUDSourceError.invalid("Cannot create fixture defaults") }
        // Empty volatile domain prevents reading saved app preferences. Writes stay
        // inside the fresh process home and suite and are removed after export.
        defaults.setVolatileDomain([:], forName: suite)
        defer { defaults.removePersistentDomain(forName: suite); defaults.removeVolatileDomain(forName: suite) }
        let configStore = ConfigurationStore(defaults: defaults); configStore.update(config)
        let controller = HUDSettingsController(store: configStore, clock: { 0 }, scheduleTimer: { _, _ in
            preconditionFailure("Reference fixture must not schedule settings timers")
        })
        let date = Date(timeIntervalSince1970: 1_700_000_000)
        var profile = UserProfile(awakeningDate: date, uid: "1000000000")
        profile.name = "Endministrator"; profile.tag = "0000"; profile.introduction = "Neutral reference profile"
        profile.birthdayMonth = 1; profile.birthdayDay = 1; profile.accumulatedWorkSeconds = 3600
        let profileDirectory = dataRoot.appendingPathComponent("Profile")
        try FileManager.default.createDirectory(at: profileDirectory, withIntermediateDirectories: true)
        try JSONEncoder().encode(ProfileArchive(version: 1, profile: profile)).write(to: profileDirectory.appendingPathComponent("profile.json"))
        let profileStore = try UserProfileStore(directory: profileDirectory, now: date)
        let personal = PersonalProfileCanvas(store: profileStore, workSeconds: { 3600 }, reduceMotion: { true })
        defer { personal.deactivate() }
        let notesStore = try NotesStore(directory: dataRoot.appendingPathComponent("Notes"))
        try notesStore.upsert(CanvasNote(id: UUID(uuidString: "00000000-0000-4000-8000-000000000001")!, kind: .text,
            text: "Neutral note\nSource-derived desktop workspace", x: 60, y: 90, width: 260, height: 140, zIndex: 1, createdAt: date))
        try notesStore.upsert(CanvasNote(id: UUID(uuidString: "00000000-0000-4000-8000-000000000002")!, kind: .todo,
            text: "Reference tasks", items: [NoteChecklistItem(id: UUID(uuidString: "00000000-0000-4000-8000-000000000003")!, text: "Compare geometry", isChecked: true),
                NoteChecklistItem(id: UUID(uuidString: "00000000-0000-4000-8000-000000000004")!, text: "Compare actions", isChecked: false)],
            x: 340, y: 90, width: 260, height: 160, zIndex: 2, createdAt: date, isPinned: true))
        let notes = NotesCanvas(store: notesStore, notesSelected: true, reduceMotion: { true })
        notes.setWorkspaceBounds(CGRect(x: 0, y: 0, width: 1920, height: 1080))
        defer { notes.setVisible(false); notes.cancelInteraction(); notes.cancelAnimations() }
        let display = HUDDisplayDescriptor(uuid: "00000000-0000-4000-8000-000000000010", name: "Reference Display", displayID: 1,
            frame: CGRect(x: 0, y: 0, width: 1920, height: 1080))
        let settingsModules: [HUDModule] = [.system, .display, .hotkeys, .about]
        let settings = settingsModules.map { HUDSettingsCanvas(module: $0, controller: controller, reduceMotion: { true }, displayProvider: { [display] }) }
        defer { settings.forEach { $0.deactivate() } }
        let host = HUDModuleContent(powerLayer: CALayer(), reduceMotion: { true })
        defer { host.cancel() }
        host.register(notes, for: .notes); host.register(personal, for: .profile)
        for canvas in settings { host.register(canvas, for: canvas.module) }
        host.update(dark: true, accent: config.accentColor, contentsScale: 2)
        let encoder = try ModuleReferenceLayerEncoder(output: output)
        try serializerFixture(encoder, output: output)
        var entries: [[String: Any]] = []
        func emit(_ module: HUDModule, state: String, actions: [[String: Any]], sliders: [[String: Any]] = [], extra: [[String: Any]] = []) throws {
            try require(host.selectedModule == module && !host.isTransitioning && host.activeTransitionAnimationCount == 0, "Module host must be settled")
            let name = "module-\(module.rawValue)-\(state).json"
            let hostRoot = try encoder.encode(host.layer, id: module.rawValue + "/" + state + "/host")
            let row: [String: Any] = ["schemaVersion": 1, "module": module.rawValue, "state": state,
                "title": module.title, "contentFrameInDesignSpace": ModuleReferenceLayerEncoder.rect(module.contentFrame),
                "hostViewportInDesignSpace": ModuleReferenceLayerEncoder.rect(HUDModuleContent.viewportFrame),
                "roots": [["space": "design-host", "layer": hostRoot]] + extra,
                "actions": actions, "sliders": sliders]
            try json(row, output.appendingPathComponent(name))
            entries.append(["module": module.rawValue, "state": state, "file": name, "actionCount": actions.count, "sliderCount": sliders.count])
        }
        host.select(module: .notes, animated: false)
        try emit(.notes, state: "default", actions: notes.accessibleActions.map {
            action($0.id, $0.label, $0.rect, space: $0.space == .workspace ? "workspace-local" : "module-local")
        }, extra: [["space": "workspace-local", "layer": try encoder.encode(notes.workspaceLayer, id: "notes/default/workspace")]])
        host.select(module: .profile, animated: false)
        try emit(.profile, state: "default", actions: personal.accessibleActions.map { action($0.id, $0.label, $0.rect) },
            sliders: personal.accessibleSliders.map { slider($0.field.rawValue, $0.label, $0.rect, $0.value, $0.minimum, $0.maximum, $0.valueDescription) },
            extra: [["space": "profile-background-local", "layer": try encoder.encode(personal.backgroundLayer, id: "profile/default/background")]])
        for canvas in settings {
            host.select(module: canvas.module, animated: false)
            for state in ["top", "bottom"] {
                if state == "bottom" { _ = canvas.scroll(at: CGPoint(x: 200, y: 100), delta: 100_000) }
                try emit(canvas.module, state: state, actions: canvas.accessibleActions.map { action($0.id, $0.label, $0.rect, enabled: $0.enabled) },
                    sliders: canvas.accessibleSliders.map { slider($0.id, $0.label, $0.rect, $0.value, $0.minimum, $0.maximum, $0.valueDescription) })
            }
        }
        let projections = try [CGSize(width: 1280, height: 800), CGSize(width: 1920, height: 1080)].map { try projection(profile: profile, size: $0) }
        let attachment = try JSONSerialization.jsonObject(with: Data(contentsOf: output.appendingPathComponent("attachment.json")))
        try require(NSApp.windows.isEmpty, "Fixture unexpectedly created a window")
        try json(["schemaVersion": 1, "scope": "actual macOS NotesCanvas, PersonalProfileCanvas, HUDSettingsCanvas and HUDModuleContent model layers",
            "sourceBaseline": "ca04f14", "entries": entries, "rasterAssets": encoder.rasterAssets, "unsupported": encoder.unsupported,
            "desktopCenterProjections": projections, "sourceDerivedAttachment": attachment,
            "coordinates": ["matrices": "column-major columns, column vectors; translation is column 3",
                "designAxes": "source design and action coordinates use top-left origin, +X right, +Y down, logical points",
                "layerGeometry": "local model layer coordinates; apply bounds origin, anchor, transform and parent sublayerTransform in CA order",
                "design-host": "1000x640 source design space before SystemHUDView reportScale/reportCenterY",
                "module-local": "add module contentFrame origin, then source report transform and desktopCenterProjection",
                "workspace-local": "1920x1080 stored overlay screen units; convert by inverse designOrigin/designScale, then actual desktopCenterProjection; no reportScale/reportCenterY",
                "profile-background-local": "separate native background; full SystemHUDView background attachment remains unverified",
                "frame": "diagnostic CA derived bounding box; do not apply frame in addition to position/bounds/anchor/transform",
                "text": "UTF-16 attributed ranges; native font names/metrics; cross-platform raster equality not verified"],
            "fixture": ["language": "english", "theme": "dark", "scale": 2, "ambientMotion": false, "reduceMotion": true,
                "profileUID": profile.uid, "profileName": profile.name, "profileTag": profile.tag, "noteCount": 2,
                "displayName": display.name, "displayFrame": ModuleReferenceLayerEncoder.rect(display.frame)] as [String: Any],
            "isolation": ["windowCreated": false, "systemHUDViewCreated": false, "temporaryStoresOnly": true,
                "isolatedPreferencesSuite": true, "liveProvidersCreated": false, "clipboardAccessed": false,
                "nativeCursorSetCount": 0, "profileActivated": false, "settingsTimersScheduled": 0],
            "notVerified": ["full SystemHUDView scene attachment and material compositing", "transition/presentation layer pixels",
                "native edit controls and settings subpages", "live providers", "Windows rendering", "cross-OS font raster equality",
                "custom layer/material features listed in unsupported", "About version metadata comes from unbundled fixture executable"]],
            output.appendingPathComponent("modules.json"))
        print("Exported \(entries.count) source module states, \(encoder.rasterAssets.count) intrinsic raster assets; \(encoder.unsupported.count) explicit unsupported/metadata entries")
    }
    static func main() { do { try run() } catch { fputs("Module reference failed: \(error)\n", stderr); exit(1) } }
}
