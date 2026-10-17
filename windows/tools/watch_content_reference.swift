import AppKit
import QuartzCore
import CryptoKit

final class ContentReferenceBackingView: NSView { override var isFlipped: Bool { true } }

@main enum WatchContentReference {
    static func check(_ value: @autoclosure () -> Bool, _ reason: String) throws {
        if !value() { throw HUDSourceError.invalid(reason) }
    }
    static func bytes(_ value: Any) throws -> Data {
        try JSONSerialization.data(withJSONObject: value, options: [.sortedKeys, .withoutEscapingSlashes])
    }
    static func write(_ value: Any, _ path: URL) throws { try bytes(value).write(to: path, options: .atomic) }
    final class Pack {
        let output: URL, encoder: ModuleReferenceLayerEncoder
        var trees: [String: [String: Any]] = [:]
        init(_ output: URL) throws {
            self.output = output; encoder = try ModuleReferenceLayerEncoder(output: output)
            try FileManager.default.createDirectory(at: output.appendingPathComponent("content"), withIntermediateDirectories: true)
        }
        func tree(_ layer: CALayer) throws -> String {
            // Local pixels only. Source world placement remains in the exact
            // typed NativeLabelPlan, and must not multiply variant count.
            let transform = layer.transform, position = layer.position, anchor = layer.anchorPoint
            let z = layer.zPosition, az = layer.anchorPointZ
            CATransaction.begin(); CATransaction.setDisableActions(true)
            layer.transform = CATransform3DIdentity; layer.position = .zero; layer.anchorPoint = .zero
            layer.zPosition = 0; layer.anchorPointZ = 0
            defer { layer.transform = transform; layer.position = position; layer.anchorPoint = anchor
                layer.zPosition = z; layer.anchorPointZ = az; CATransaction.commit() }
            let value = try encoder.encode(layer, id: "content")
            let data = try bytes(value), digest = ModuleReferenceLayerEncoder.sha256(data)
            if trees[digest] == nil {
                let file = "content/" + digest + ".json"
                try data.write(to: output.appendingPathComponent(file), options: .atomic)
                trees[digest] = ["file": file, "sha256": digest, "bytes": data.count]
            }
            return digest
        }
    }
    static func run() throws {
        try check(Thread.isMainThread && CommandLine.arguments.contains("--ui-test")
                  && ProcessInfo.processInfo.environment["CFFIXED_USER_HOME"] != nil, "Use the isolated main-thread wrapper")
        var args = Array(CommandLine.arguments.dropFirst()), output: URL?, titles = ["Sample App", "A longer synthetic application title"], allSlots = false
        var accent = "FAD41F"
        while !args.isEmpty {
            let option = args.removeFirst()
            if option == "--ui-test" { continue }
            if option == "--all-right-slots" { allSlots = true; continue }
            try check(!args.isEmpty, "Missing argument for " + option); let value = args.removeFirst()
            if option == "--output" { output = URL(fileURLWithPath: value, isDirectory: true) }
            else if option == "--custom-titles" {
                let data = try Data(contentsOf: URL(fileURLWithPath: value)); try check(data.count <= 65536, "Custom fixture file exceeds 64 KiB")
                guard let supplied = try JSONSerialization.jsonObject(with: data) as? [String] else { throw HUDSourceError.invalid("Custom fixtures must be a JSON string array") }
                titles = supplied
            } else if option == "--accent" { accent = value.uppercased() }
            else { throw HUDSourceError.invalid("Unknown content export option: " + option) }
        }
        guard let output else { throw HUDSourceError.invalid("Missing --output") }
        try check(titles.count <= 32 && Set(titles).count == titles.count && titles.allSatisfy {
            (1...128).contains($0.count) && !$0.contains(where: { $0.isNewline }) && !$0.contains("\0")
        }, "Custom title fixtures need unique 1–128 character strings without line breaks")
        try check(accent.count == 6 && accent.allSatisfy { $0.isHexDigit && $0.isASCII }, "Accent must be six hexadecimal digits")
        NSApplication.shared.setActivationPolicy(.prohibited); NSApp.appearance = NSAppearance(named: .darkAqua)
        L10n.language = .english
        var config = AppConfiguration.defaults; config.language = .english; config.theme = .dark
        config.ambientAnimation = false; config.reduceMotion = true; config.blurAmount = 0; config.accentHex = accent
        HUDRuntimeAppearance.configuration = config
        let view = try HUDSourceWatchView(frame: CGRect(x: 0, y: 0, width: 1280, height: 800), desktopMode: true,
            desktopNavigationEntries: HUDDesktopWatchNavigation.entries(shortcuts: []))
        defer { view.conceal() }
        view.pointerLocationProvider = { CGPoint(x: 640, y: 400) }; view.isDesktopPointerLocked = { true }
        var profile = UserProfile(awakeningDate: Date(timeIntervalSince1970: 0), uid: "1000000000")
        profile.name = "Endministrator"; profile.tag = "0000"; view.setDesktopProfile(profile, avatar: nil, background: nil)
        view.showStable(); view.layoutSubtreeIfNeeded(); view.layout()
        guard let frame = view.currentFrameForVerification, let camera = view.currentCameraForVerification else { throw HUDSourceError.invalid("Detached desktop frame did not resolve") }
        try check(view.document.widgets == nil, "Raw game widgets cannot supply desktop content")
        let slots = view.contentReferenceSlots, bindings = view.contentReferenceInitialBindings
        let right = slots.filter { $0["group"] as? String == "right" }
        try check(slots.count == 24 && right.count == 18 && bindings.count == 24, "Original source desktop pool changed")
        let pack = try Pack(output)
        let languages: [AppLanguage] = [.english, .simplifiedChinese, .traditionalChinese, .japanese, .korean]
        var variants: [[String: Any]] = [], custom: [[String: Any]] = [], icons: [[String: Any]] = [], chromeRows: [[String: Any]] = []
        let shortcutID = UUID(uuidString: "00000000-0000-0000-0000-000000000001")!
        for language in languages {
            L10n.language = language; config.language = language; HUDRuntimeAppearance.configuration = config
            let entries = HUDDesktopWatchNavigation.entries(shortcuts: [])
            for dark in [true, false] {
                NSApp.appearance = NSAppearance(named: dark ? .darkAqua : .aqua)
                for entry in entries {
                    let candidates = (entry.target == .module(.addApp) || (allSlots && entry.target.group != .bottom && !entries.prefix(4).contains(where: { $0.target == entry.target })))
                        ? right : slots.filter { bindings[$0["buttonID"] as! String] == entry.target.identifier }
                    for slot in candidates { for selected in [false, true] {
                        let id = slot["buttonID"] as! String
                        let layers = try view.contentReferenceBind(entry, to: HUDSourceID(rawValue: id), selected: selected, dark: dark, frame: frame, camera: camera)
                        variants.append(["language": language.rawValue, "theme": dark ? "dark" : "light", "selected": selected,
                            "target": entry.target.identifier, "buttonID": id, "caption": try pack.tree(layers.caption), "icon": try pack.tree(layers.icon)])
                    }}
                }
                let chrome = ChromeSourceFixture(); chrome.configuration = config; chrome.configuration.theme = dark ? .dark : .light
                chrome.layout(); chrome.applyAppearance(dark: dark)
                let backing = ContentReferenceBackingView(frame: view.bounds); backing.wantsLayer = true; backing.layer!.addSublayer(chrome.canvas)
                chrome.workModeController.snapshot.phase = .running; chrome.updateWorkPresentation(force: true)
                let active = chrome.workBadge.string as? String ?? ""
                chrome.workModeController.snapshot.phase = .paused; chrome.updateWorkPresentation(force: true)
                let paused = chrome.workBadge.string as? String ?? ""
                chromeRows.append(["language": language.rawValue, "theme": dark ? "dark" : "light",
                    "header": try pack.tree(chrome.header), "footer": try pack.tree(chrome.footer),
                    "workActive": active, "workPaused": paused,
                    "moduleTitles": HUDModule.allCases.map { ["target": $0.rawValue, "title": $0.title, "navigationTitle": $0.navigationTitle] }])
            }
            // Custom apps are never a selected HUD module. Actual titles are
            // bounded explicit fixtures, never installed-app or saved-user data.
            for (index, title) in titles.enumerated() { for slot in right {
                let shortcut = HUDAppShortcutPresentation(id: shortcutID, name: title, iconPreset: .original, icon: nil)
                let entry = HUDDesktopWatchNavigation.Entry(sourceName: "SyntheticCustomTitle", target: .appShortcut(shortcutID), title: title, shortcut: shortcut)
                let id = slot["buttonID"] as! String
                let layers = try view.contentReferenceBind(entry, to: HUDSourceID(rawValue: id), selected: false, dark: true, frame: frame, camera: camera)
                custom.append(["language": language.rawValue, "titleIndex": index, "buttonID": id, "caption": try pack.tree(layers.caption)])
            }}
            print("Captured source local content: \(language.rawValue)")
        }
        L10n.language = .english
        for preset in AppShortcutIcon.allCases { for slot in right {
            let shortcut = HUDAppShortcutPresentation(id: shortcutID, name: "Synthetic icon fixture", iconPreset: preset, icon: nil)
            let entry = HUDDesktopWatchNavigation.Entry(sourceName: "SyntheticIcon", target: .appShortcut(shortcutID), title: shortcut.name, shortcut: shortcut)
            let id = slot["buttonID"] as! String
            let layers = try view.contentReferenceBind(entry, to: HUDSourceID(rawValue: id), selected: false, dark: true, frame: frame, camera: camera)
            icons.append(["preset": preset.rawValue, "buttonID": id, "icon": try pack.tree(layers.icon)])
        }}
        view.conceal()
        try check(view.window == nil && !view.hasDisplayTimerForVerification && !view.backdropPreparingForVerification
                  && view.sourceCursorSetCountForVerification == 0, "Exporter escaped detached fixture boundaries")
        let result: [String: Any] = ["schemaVersion": 1, "desktopMode": true, "matrix": allSlots ? "all-right-slots" : "source-navigation-minimal",
            "accentHex": accent, "slots": slots, "initialBindings": bindings, "variants": variants,
            "customTitles": titles, "customCaptions": custom, "shortcutIcons": icons, "chrome": chromeRows,
            "contentTrees": pack.trees, "rasterAssets": pack.encoder.rasterAssets, "unsupported": pack.encoder.unsupported,
            "safety": ["systemHUDViewConstructed": false, "windowCreated": false, "liveProviders": false, "cursorSets": 0, "installedAppsRead": false, "userStoresRead": false],
            "limits": ["Custom title coverage is exact only for the explicit fixture strings; arbitrary future titles require source fitting or a separately validated native text fitting implementation",
                       "Original custom-app images are not read; original preset exports the source nil-image vector fallback",
                       "Selected colors are exact for the requested accent; arbitrary accent changes require source color-role evaluation",
                       "Header and work badge literals remain in the source language; no translations are invented",
                       "Cross-OS font raster equality is not claimed; no full frame/oracle pixel data is duplicated"]]
        try write(result, output.appendingPathComponent("watch-content.json"))
        print("Exported \(variants.count) source variants, \(custom.count) explicit custom captions, \(icons.count) preset/slot observations; \(pack.trees.count) unique local trees")
    }
    static func main() { do { try run() } catch { fputs("Source content export failed: \(error)\n", stderr); exit(1) } }
}
