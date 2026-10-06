import AppKit
import QuartzCore

/// Reads actual retained text layers. Fixtures never activate telemetry, audio,
/// clipboard observation, notification scheduling or production preferences.
enum HUDSectionAppearanceTests {
    private final class NoopTimer: HUDSettingsTimer { func invalidate() {} }
    static func run() -> Int {
        _ = NSApplication.shared
        var count = 0
        func check(_ condition: Bool, _ message: String) { count += 1; precondition(condition, message) }
        func texts(_ layer: CALayer) -> [CATextLayer] {
            let own = (layer as? CATextLayer).map { [$0] } ?? []
            return own + (layer.sublayers ?? []).flatMap(texts)
        }
        let directory = FileManager.default.temporaryDirectory.appendingPathComponent("HUDSectionAppearance-\(UUID())")
        let suite = "HUDSectionAppearance.\(UUID())"
        let defaults = UserDefaults(suiteName: suite)!
        let previousLanguage = L10n.language
        defer { defaults.removePersistentDomain(forName: suite); L10n.language = previousLanguage; try? FileManager.default.removeItem(at: directory) }
        for title in ["Notes", "事件日志", "// Storage", "// 既有标题"] {
            let decorated = HUDSectionHeading.text(title)
            check(decorated.hasPrefix("// ") && HUDSectionHeading.text(decorated) == decorated,
                  "Section decoration is idempotent for both localized and already-prefixed headings")
        }
        let storedConfiguration = ConfigurationStore(defaults: defaults)
        let settings = HUDSettingsController(store: storedConfiguration, scheduleTimer: { _, _ in NoopTimer() })
        let profileStore = try! UserProfileStore(directory: directory.appendingPathComponent("Profile"))
        try! profileStore.update { $0.name = "Fixture profile" }
        let archiveStore = ArchiveStore(directory: directory.appendingPathComponent("Archive"))
        for language: AppLanguage in [.english, .simplifiedChinese] {
            L10n.language = language
            let navigationTitles = HUDModule.allCases.map(\.title)
            for dark in [true, false] {
                let style = HUDModuleContentStyle(dark: dark, accent: .systemYellow, contentsScale: 2)
                let canvases: [(HUDModule, HUDModuleContentFactory, String)] = [
                    (.notes, NotesCanvas(store: nil, reduceMotion: { true }), L10n.text("NOTES", "便笺")),
                    (.fileShelf, FileShelfCanvas(store: nil, reduceMotion: { true }), HUDModule.fileShelf.title),
                    (.clipboard, ClipboardCanvas(store: ClipboardStore(), reduceMotion: { true }), HUDModule.clipboard.title),
                    (.volume, VolumeCanvas(controller: .fixture()), HUDModule.volume.title),
                    (.storage, StorageCanvas(controller: .fixture(), reduceMotion: { true }), HUDModule.storage.title),
                    (.activityMonitor, ActivityMonitorCanvas(controller: .fixture(), reduceMotion: { true }), HUDModule.activityMonitor.title),
                    (.eventLog, EventLogCanvas(store: SystemEventLog(directory: directory.appendingPathComponent("Events")), reduceMotion: { true }), HUDModule.eventLog.title),
                    (.addApp, AppShortcutCanvas(store: nil, reduceMotion: { true }), L10n.text("APPLICATIONS", "应用快捷方式")),
                    (.archive, ArchiveCanvas(controller: ArchiveController(store: archiveStore)), HUDModule.archive.title),
                    (.calendar, HUDCalendarCanvas(controller: .fixture()), HUDModule.calendar.title),
                    (.mediaAssembly, MediaAssemblyCanvas(controller: MediaAssemblyController()), HUDModule.mediaAssembly.title)
                ]
                for (module, canvas, title) in canvases {
                    let labels = texts(canvas.makeContent(for: module, style: style))
                    let heading = labels.first { ($0.string as? String) == "// " + title }
                    check(heading != nil, "The \(module.rawValue) canvas renders its localized section heading exactly once")
                    if let color = heading?.foregroundColor.flatMap(NSColor.init(cgColor:))?.usingColorSpace(.deviceRGB) {
                        check(dark ? color.redComponent > 0.5 : color.redComponent < 0.5,
                              "The \(module.rawValue) heading keeps contrast in its actual dark/light canvas")
                    } else { check(false, "Section heading must expose its rendered ink color") }
                }
                for module: HUDModule in [.system, .display, .hotkeys, .about] {
                    let canvas = HUDSettingsCanvas(module: module, controller: settings, reduceMotion: { true }, displayProvider: { [] })
                    let labels = texts(canvas.makeContent(for: module, style: style)).compactMap { $0.string as? String }
                    check(labels.contains(module.title) && !labels.contains("// " + module.title),
                          "Settings retain their existing undecorated \(module.rawValue) page heading")
                }
                let profile = PersonalProfileCanvas(store: profileStore, reduceMotion: { true })
                let profileText = texts(profile.makeContent(for: .profile, style: style)).compactMap { $0.string as? String }
                check(profileText.contains("Fixture profile#0000") && !profileText.contains(where: { $0.hasPrefix("//") }),
                      "Profile keeps its existing user identity layout without section decoration")
                profile.deactivate()
            }
            check(HUDModule.allCases.map(\.title) == navigationTitles && navigationTitles.allSatisfy { !$0.hasPrefix("//") },
                  "Decorating center headings does not change navigation labels or persisted module identity")
        }
        return count
    }
}
