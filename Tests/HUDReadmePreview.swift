import AppKit
import QuartzCore

/// Reproducible documentation previews, drawn by the real HUD. Every data
/// source is a fixture; this tool never reads the user's clipboard or stores.
@main
enum HUDReadmePreview {
    static func main() throws {
        let app = NSApplication.shared
        app.setActivationPolicy(.accessory)
        let output = URL(fileURLWithPath: CommandLine.arguments[1], isDirectory: true)
        let fm = FileManager.default
        try fm.createDirectory(at: output, withIntermediateDirectories: true)
        let root = fm.temporaryDirectory.appendingPathComponent("EndfieldHUD-Readme-\(UUID().uuidString)")
        try fm.createDirectory(at: root, withIntermediateDirectories: true)
        defer { try? fm.removeItem(at: root) }
        let suite = "EndfieldHUD.Readme.\(UUID().uuidString)"
        let defaults = UserDefaults(suiteName: suite)!
        defer { defaults.removePersistentDomain(forName: suite) }
        let configurationStore = ConfigurationStore(defaults: defaults)
        let settings = HUDSettingsController(store: configurationStore)
        settings.update { c in
            c.language = .english; c.blurAmount = 0; c.closeOnFocusLost = false
            c.launchAtLogin = false; c.ambientAnimation = true
        }
        L10n.language = .english
        let notes = try NotesStore(directory: root.appendingPathComponent("Notes"))
        try notes.upsert(CanvasNote(kind: .text, text: "A little space for your next idea.\n\nDrag notes anywhere on the screen.",
                                   x: 420, y: 286, width: 242, height: 152))
        try notes.upsert(CanvasNote(kind: .todo, items: [NoteChecklistItem(text: "Plan the day", isChecked: true),
                                                      NoteChecklistItem(text: "Make something useful"),
                                                      NoteChecklistItem(text: "Take a break")],
                                   x: 654, y: 340, width: 244, height: 165, zIndex: 1))
        let shelf = try FileShelfStore(directory: root.appendingPathComponent("Shelf"))
        let document = root.appendingPathComponent("Project notes.txt")
        try "Sample document for the README preview.\n".write(to: document, atomically: true, encoding: .utf8)
        _ = try shelf.add(urls: [document])
        let pasteboard = NSPasteboard.withUniqueName()
        defer { pasteboard.releaseGlobally() }
        let clipboardStore = ClipboardStore()
        for sample in ["https://github.com/DDDuoDuo/EndfieldHUD", "Small steps, steady progress.", "A place for notes, tools, and your next idea."] {
            pasteboard.clearContents(); pasteboard.setString(sample, forType: .string)
            clipboardStore.capture(from: pasteboard)
        }
        let clipboard = ClipboardWatcher(store: clipboardStore, pasteboard: pasteboard)
        let profileDirectory = root.appendingPathComponent("Profile")
        try fm.createDirectory(at: profileDirectory, withIntermediateDirectories: true)
        var profileData = UserProfile(awakeningDate: Date(timeIntervalSince1970: 1_798_588_800), uid: "1000000000")
        profileData.introduction = "Make room for your next idea."
        profileData.accumulatedWorkSeconds = 12 * 3600
        struct ProfileArchive: Encodable { let version = 1; let profile: UserProfile }
        try JSONEncoder().encode(ProfileArchive(profile: profileData)).write(to: profileDirectory.appendingPathComponent("profile.json"))
        let profile = try UserProfileStore(directory: profileDirectory)
        let shortcuts = try AppShortcutStore(directory: root.appendingPathComponent("Shortcuts"))
        // Public system apps supply their own icons. They are never launched.
        for (name, preset) in [("Notes", AppShortcutIcon.original), ("Music", .music)] {
            let url = URL(fileURLWithPath: "/System/Applications/\(name).app")
            if fm.fileExists(atPath: url.path) {
                let candidate = try shortcuts.inspect(url: url)
                _ = try shortcuts.save(candidate: candidate, name: name, iconPreset: preset)
            }
        }
        let events = SystemEventLog()
        events.record(kind: .overlayOpened)
        events.record(kind: .shelfAdded, metadata: ["filename": "Project notes.txt", "count": "1"])
        events.record(kind: .clipboardCopied, metadata: ["kind": "text"])
        events.record(kind: .workStarted, metadata: ["kind": "countdown", "seconds": "1500"])
        let work = WorkModeController()
        let map = try WorldMapStore(directory: root.appendingPathComponent("Map"))
        let snapshot = BatterySnapshot(percentage: 82, isPluggedIn: true, isCharging: true,
                                       isFullyCharged: false, hasBattery: true, healthCategory: "Normal")
        let view = SystemHUDView(frame: NSRect(x: 0, y: 0, width: 1280, height: 800),
                                 notesStore: .success(notes), shelfStore: .success(shelf), clipboard: clipboard,
                                 audio: .fixture(), perAppAudio: .fixture(), workMode: work, eventLog: events,
                                 storage: .fixture(), activity: .fixture(), appActivity: .fixture(),
                                 appShortcuts: .success(shortcuts), settings: settings, profile: .success(profile),
                                 mapStore: .success(map), initialConfiguration: settings.configuration,
                                 initialSnapshot: snapshot, initialModule: .power)
        let window = NSWindow(contentRect: view.bounds, styleMask: .borderless, backing: .buffered, defer: false)
        window.isReleasedWhenClosed = false
        window.contentView = view
        window.backgroundColor = NSColor(srgbRed: 0.055, green: 0.065, blue: 0.075, alpha: 1)
        // The isolated window makes Core Animation presentation layers available;
        // images are rendered from our layers, never from the desktop framebuffer.
        window.orderBack(nil)
        defer { view.cancelAnimations(); window.close() }
        var pointer = CGPoint(x: 0.15, y: -0.12)
        view.pointerLocationProvider = {
            window.convertPoint(toScreen: CGPoint(x: 640 + pointer.x * 640, y: 400 - pointer.y * 400))
        }
        view.interactionEnabled = true
        view.layoutSubtreeIfNeeded()
        view.showStable()
        pump(0.7)
        let background = NSColor(srgbRed: 0.055, green: 0.065, blue: 0.075, alpha: 1).cgColor
        func still(_ name: String) throws {
            try view.writePNG(to: output.appendingPathComponent(name + ".png"), scale: 1, presentation: true, background: background)
        }
        try still("overview")
        let sections: [(String, HUDModule)] = [("notes", .notes), ("shelf", .fileShelf), ("clipboard", .clipboard),
            ("volume", .volume), ("work-mode", .workMode), ("event-log", .eventLog), ("storage", .storage),
            ("activity", .activityMonitor), ("apps", .addApp), ("settings", .display), ("profile", .profile), ("map", .map)]
        for (name, module) in sections {
            if module == .workMode { work.start() }
            view.selectModule(module, animated: true)
            pump(module == .map ? 1.8 : 0.7)
            try still(name)
            if module == .workMode { work.reset() }
        }
        view.selectModule(.system, animated: true); pump(0.6)
        view.performSettingsActionForVerification("language"); pump(0.6)
        try still("languages")
        for (name, language) in [("traditional-chinese", AppLanguage.traditionalChinese), ("japanese", .japanese)] {
            settings.update { $0.language = language }
            L10n.language = language
            view.set(snapshot: snapshot, configuration: settings.configuration)
            view.selectModule(.display, animated: false); pump(0.4)
            try still(name)
        }
        settings.update { $0.language = .english }; L10n.language = .english
        view.set(snapshot: snapshot, configuration: settings.configuration)
        view.selectModule(.activityMonitor, animated: false); pump(0.4)
        for directory in ["motion", "modules"] {
            try fm.createDirectory(at: output.appendingPathComponent(directory), withIntermediateDirectories: true)
        }
        view.animateExit {}; pump(SystemHUDView.exitDuration + 0.1)
        view.animateEntrance {}
        for frame in 0..<72 {
            let time = Double(frame) / 12
            if frame == 50 { view.animateExit {} }
            if frame == 61 { view.animateEntrance {} }
            pointer = CGPoint(x: sin(time * 1.3) * 0.85, y: cos(time * 1.15) * 0.7)
            view.setPointerForVerification(pointer)
            pump(1.0 / 12)
            try view.writePNG(to: output.appendingPathComponent(String(format: "motion/%03d.png", frame)),
                              scale: 0.75, presentation: true, background: background)
        }
        view.showStable(); pointer = .zero
        let sequence: [HUDModule] = [.notes, .volume, .workMode, .storage, .activityMonitor, .profile, .map]
        for frame in 0..<(sequence.count * 14) {
            if frame % 14 == 0 {
                let module = sequence[frame / 14]
                if module == .workMode { work.start() } else { work.reset() }
                view.selectModule(module, animated: true)
            }
            pump(1.0 / 12)
            try view.writePNG(to: output.appendingPathComponent(String(format: "modules/%03d.png", frame)),
                              scale: 0.75, presentation: true, background: background)
        }
        print("Rendered fixture previews to \(output.path)")
    }

    static func pump(_ duration: TimeInterval) {
        let end = Date().addingTimeInterval(duration)
        while Date() < end { RunLoop.main.run(until: min(end, Date().addingTimeInterval(0.008))) }
    }
}
