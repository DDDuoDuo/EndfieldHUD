import AppKit
import QuartzCore

// Actual-source oracle of the redesigned App Shortcuts screen (Mac 1.3.0 Sources/AppShortcutCanvas.swift,
// AppShortcutArtwork.swift and HUDSourceDesktopNavigationLayout.swift's HUDSourceDesktopIconLayout) with the
// real AppShortcutStore over synthetic application bundles in the temporary home. Exported:
//  - "presets": every preset's tile artwork exactly as AppShortcutCanvas.artwork places it (the trimmed raster
//    of AppShortcutArtwork.placedImage in the 40 x 40 tile's 30-point box: near-black #111 for the presets on
//    the accent tile, white .93 for the App icon fallback glyph), written as presets/<preset>.png (the Windows
//    resources/app-shortcut set) with its pixel size, frame and localized titles;
//  - "glyphFrames": HUDSourceDesktopIconLayout.glyphFrame samples; "trim": HUDSourceDesktopIconLayout.image of
//    synthetic icons and AppShortcutCanvas.averageColor of a deterministic 96 x 96 icon;
//  - "states": the canvas (empty, list, hover, icon menu, drop target, Light, zh-Hans, unavailable store, an
//    added app revealing the end of a long list) as its model layer tree, controls, accessibility items,
//    header, toast and confirm request, with the scenario INPUT (records, presets, icon presence and the
//    tile colours the Mac computed from the system icons, which the Windows suite injects).
// Isolation: temporary CFFIXED_USER_HOME; synthetic .app bundles there (never launched; LaunchServices is
// never asked to open anything); no window, no input posted; Reduce Motion on so every page installs at once.

private var reduce = true
private let env = HUDDesignEnvironment(renderScale: 2, reduceMotion: { reduce })

private func pngData(_ image: CGImage) -> Data { NSBitmapImageRep(cgImage: image).representation(using: .png, properties: [:])! }
private func pixels(_ image: CGImage) -> [UInt8] {
    var out = [UInt8](repeating: 0, count: image.width * image.height * 4)
    let ctx = CGContext(data: &out, width: image.width, height: image.height, bitsPerComponent: 8, bytesPerRow: image.width * 4,
                        space: CGColorSpace(name: CGColorSpace.sRGB)!, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
    ctx.draw(image, in: CGRect(x: 0, y: 0, width: image.width, height: image.height))
    return out
}
/// The deterministic 96 x 96 straight-alpha icon the Windows suite generates too.
private func patternIcon() -> CGImage {
    var bytes = [UInt8](repeating: 0, count: 96 * 96 * 4)
    for y in 0..<96 { for x in 0..<96 {
        let i = (y * 96 + x) * 4
        if x >= 10 && x < 86 && y >= 20 && y < 70 { bytes[i] = UInt8(x * 2); bytes[i + 1] = UInt8(y * 3); bytes[i + 2] = 128; bytes[i + 3] = 255 }
        else if x == 5 && y == 5 { bytes[i] = 255; bytes[i + 3] = 6 }
    } }
    let provider = CGDataProvider(data: Data(bytes) as CFData)!
    return CGImage(width: 96, height: 96, bitsPerComponent: 8, bitsPerPixel: 32, bytesPerRow: 96 * 4, space: CGColorSpace(name: CGColorSpace.sRGB)!,
                   bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.last.rawValue), provider: provider, decode: nil, shouldInterpolate: true,
                   intent: .defaultIntent)!
}
private func application(_ root: URL, _ name: String, identifier: String) throws -> URL {
    let app = root.appendingPathComponent(name + ".app", isDirectory: true)
    let macos = app.appendingPathComponent("Contents/MacOS", isDirectory: true)
    try FileManager.default.createDirectory(at: macos, withIntermediateDirectories: true)
    let info: [String: Any] = ["CFBundlePackageType": "APPL", "CFBundleExecutable": "run", "CFBundleName": name, "CFBundleIdentifier": identifier]
    try PropertyListSerialization.data(fromPropertyList: info, format: .xml, options: 0).write(to: app.appendingPathComponent("Contents/Info.plist"))
    let executable = macos.appendingPathComponent("run")
    try Data("#!/bin/sh\nexit 0\n".utf8).write(to: executable)
    try FileManager.default.setAttributes([.posixPermissions: 0o755], ofItemAtPath: executable.path)
    return app
}

@main private enum AppShortcutReference {
    static func main() throws {
        let home = ProcessInfo.processInfo.environment["CFFIXED_USER_HOME"]!
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

        // MARK: Preset artwork (AppShortcutCanvas.artwork, AppShortcutArtwork.placedImage)
        let presetsDirectory = output.appendingPathComponent("presets", isDirectory: true)
        try FileManager.default.createDirectory(at: presetsDirectory, withIntermediateDirectories: true)
        var presets: [[String: Any]] = []
        for preset in AppShortcutIcon.allCases {
            let tint = preset == .original ? NSColor(white: 0.93, alpha: 1) : NSColor(cgColor: HUDColor.nearBlack) ?? .black
            let box = CGRect(x: 5, y: 5, width: 30, height: 30)
            guard let placed = AppShortcutArtwork.placedImage(for: preset, in: box, color: tint) else { preconditionFailure("no artwork for \(preset)") }
            let file = preset.rawValue + ".png"
            try pngData(placed.image).write(to: presetsDirectory.appendingPathComponent(file))
            var titles: [String: String] = [:]
            for language in [AppLanguage.english, .simplifiedChinese, .traditionalChinese, .japanese, .korean] {
                L10n.language = language; titles[languageName(language)] = preset.title
            }
            L10n.language = .english
            presets.append(["id": preset.rawValue, "file": "presets/" + file, "size": [placed.image.width, placed.image.height],
                            "frame": rect(placed.frame), "tint": rgba(tint.cgColor), "titles": titles,
                            "game": AppShortcutArtwork.gameIcon(for: preset) != nil])
        }
        payload["presets"] = presets

        // MARK: Icon layout (HUDSourceDesktopIconLayout)
        payload["glyphFrames"] = [[96, 96], [60, 40], [40, 60], [12, 8], [1, 50], [30, 30], [76, 50]].map { size -> [String: Any] in
            let s = CGSize(width: size[0], height: size[1])
            return ["size": size, "glyph": rect(HUDSourceDesktopIconLayout.glyphFrame(s))]
        }
        let bitmap = CGContext(data: nil, width: 12, height: 8, bitsPerComponent: 8, bytesPerRow: 48, space: CGColorSpace(name: CGColorSpace.sRGB)!,
                               bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
        bitmap.setFillColor(CGColor(srgbRed: 1, green: 0, blue: 0, alpha: 1)); bitmap.fill(CGRect(x: 2, y: 1, width: 8, height: 6))
        let small = NSImage(cgImage: bitmap.makeImage()!, size: CGSize(width: 12, height: 8))
        let smallTrim = AppShortcutArtwork.image(for: .original, original: small, size: 96, color: .white).map(HUDSourceDesktopIconLayout.image)!
        let pattern = patternIcon()
        let patternTrim = HUDSourceDesktopIconLayout.image(pattern)
        let average = AppShortcutCanvas.averageColor(pattern)!
        payload["trim"] = ["small": ["size": [smallTrim.width, smallTrim.height]],
                           "pattern": ["size": [patternTrim.width, patternTrim.height], "average": rgba(average),
                                       "trimmedPixels": pixels(patternTrim).count]]

        // MARK: Canvas states (real store, synthetic bundles)
        let root = URL(fileURLWithPath: home).appendingPathComponent("oracle", isDirectory: true)
        let apps = root.appendingPathComponent("Applications", isDirectory: true)
        try FileManager.default.createDirectory(at: apps, withIntermediateDirectories: true)
        let style = HUDModuleContentStyle(dark: true, accent: .yellow, contentsScale: 2)
        var states: [[String: Any]] = []
        func input(_ canvas: AppShortcutCanvas, _ store: AppShortcutStore?) -> [String: Any] {
            ["items": canvas.items.map { item -> [String: Any] in
                var row: [String: Any] = ["id": item.id.uuidString, "name": item.name, "preset": item.iconPreset.rawValue]
                let icon = store?.icon(for: item.id)
                row["hasIcon"] = icon != nil
                if let icon {
                    var proposed = CGRect(x: 0, y: 0, width: 96, height: 96)
                    let image = icon.cgImage(forProposedRect: &proposed, context: nil, hints: nil)
                    row["tileColor"] = rgba(AppShortcutCanvas.averageColor(image) ?? HUDColor.rgb(0x2A2A2A))
                }
                return row
            }, "store": store != nil]
        }
        func record(_ canvas: AppShortcutCanvas, _ store: AppShortcutStore?, _ context: HUDDesignContext, state: String,
                    language: AppLanguage = .english, light: Bool = false, extra: [String: Any] = [:]) throws {
            var row: [String: Any] = ["state": state, "language": languageName(language), "light": light,
                "layer": try recorder.tree(canvas.root, id: "root"),
                "controls": canvas.page.controls.map { controlState($0, in: canvas.root) },
                "accessibility": accessibility(canvas.accessibilityItems), "input": input(canvas, store),
                "header": ["counter": canvas.header.counterTextForTesting, "title": canvas.header.titleTextForTesting.0],
                "dropTarget": canvas.isDropTarget, "inputLocked": canvas.isInputLocked,
                "offsets": canvas.page.scrollAreas.map { ["id": $0.id, "offset": $0.area.offset, "maximum": $0.area.maximumOffset] }]
            if canvas.isMenuOpen { row["top"] = try recorder.tree(canvas.top, id: "top") }
            if let toast = context.overlay.toastState.current {
                row["toast"] = ["icon": toast.icon.rawValue, "title": toast.title, "caption": toast.caption]
            }
            if let request = context.overlay.confirmState.request, context.overlay.confirmState.isOpen {
                row["confirm"] = ["id": request.id, "title": request.title, "message": request.message, "confirm": request.confirmTitle,
                                  "cancel": request.cancelTitle, "danger": request.danger, "icon": request.icon?.rawValue as Any? ?? NSNull()]
            }
            for (k, v) in extra { row[k] = v }
            states.append(row)
        }
        func show(_ canvas: AppShortcutCanvas) { _ = canvas.makeContent(for: .addApp, style: style) }
        func control(_ canvas: AppShortcutCanvas, _ id: String) -> HUDControl { canvas.page.controls.first { $0.id == id }! }

        // Empty store.
        do {
            let context = HUDDesignContext(env: env)
            let store = try AppShortcutStore(directory: root.appendingPathComponent("Empty", isDirectory: true))
            let canvas = AppShortcutCanvas(context: context, store: store)
            show(canvas); canvas.setActive(true)
            try record(canvas, store, context, state: "empty")
        }
        // Unavailable store.
        do {
            let context = HUDDesignContext(env: env)
            let canvas = AppShortcutCanvas(context: context, store: nil, error: "Synthetic store failure")
            show(canvas); canvas.setActive(true)
            try record(canvas, nil, context, state: "unavailable")
        }
        // A list: an App icon app with its icon, Camera, Bolt (game artwork), Music, and an App icon app
        // whose bundle went missing (the fallback glyph).
        let listDirectory = root.appendingPathComponent("List", isDirectory: true)
        let store = try AppShortcutStore(directory: listDirectory)
        let names = ["Alpha", "Bravo", "Charlie", "Delta", "Echo"]
        let presetsFor: [AppShortcutIcon] = [.original, .camera, .bolt, .music, .original]
        var saved: [AppShortcut] = []
        for (n, name) in names.enumerated() {
            let url = try application(apps, name, identifier: "test.oracle." + name.lowercased())
            let candidate = try store.inspect(url: url)
            saved.append(try store.save(candidate: candidate, name: name + " App", iconPreset: presetsFor[n]))
        }
        try FileManager.default.removeItem(at: apps.appendingPathComponent("Echo.app"))
        let reloaded = try AppShortcutStore(directory: listDirectory) // forget the cached icons: Echo resolves to nothing now
        let context = HUDDesignContext(env: env)
        let canvas = AppShortcutCanvas(context: context, store: reloaded)
        var launched: [String] = []
        canvas.onLaunch = { launched.append($0.uuidString) }
        show(canvas); canvas.setActive(true)
        try record(canvas, reloaded, context, state: "list")
        let firstTile = control(canvas, "apps:\(saved[0].id.uuidString):icon")
        HUDControlHighlightLayer.update(in: canvas.root, point: centre(firstTile, in: canvas.root), pressed: false)
        try record(canvas, reloaded, context, state: "hoverTile")
        HUDControlHighlightLayer.update(in: canvas.root, point: nil, pressed: false)
        let menuTile = control(canvas, "apps:\(saved[1].id.uuidString):icon")
        menuTile.onActivate?()
        try record(canvas, reloaded, context, state: "iconMenu", extra: ["menuFor": saved[1].id.uuidString])
        canvas.closeMenu()
        canvas.setDropTarget(true)
        try record(canvas, reloaded, context, state: "dropTarget")
        canvas.setDropTarget(false)
        control(canvas, "apps:\(saved[2].id.uuidString):open").onActivate?()
        try record(canvas, reloaded, context, state: "opening", extra: ["launched": launched])
        context.overlay.clearToast()
        control(canvas, "apps:\(saved[3].id.uuidString):remove").onActivate?()
        try record(canvas, reloaded, context, state: "confirmRemove")
        context.overlay.dismissConfirm()
        HUDColor.apply(accentHex: "FAD41F", light: true); context.refreshPalette(); show(canvas)
        try record(canvas, reloaded, context, state: "list", light: true)
        HUDColor.apply(accentHex: "FAD41F", light: false); context.refreshPalette(); show(canvas)
        L10n.language = .simplifiedChinese; canvas.setActive(false); show(canvas); canvas.setActive(true)
        try record(canvas, reloaded, context, state: "list", language: .simplifiedChinese)
        L10n.language = .english; canvas.setActive(false); show(canvas); canvas.setActive(true)
        // A long list, then an added app reveals the end of the list (AppShortcutCanvas.importURLs).
        for n in 0..<6 {
            let url = try application(apps, "More\(n)", identifier: "test.oracle.more\(n)")
            _ = try reloaded.save(candidate: try reloaded.inspect(url: url), name: "More \(n)", iconPreset: n % 2 == 0 ? .grid : .play)
        }
        canvas.reload()
        try record(canvas, reloaded, context, state: "long")
        let added = try application(apps, "Foxtrot", identifier: "test.oracle.foxtrot")
        var savedNotice: [String] = []
        canvas.onSaved = { savedNotice.append($0.name) }
        _ = canvas.importURLs([added])
        try record(canvas, reloaded, context, state: "added", extra: ["saved": savedNotice])
        context.overlay.clearToast()
        _ = canvas.importURLs([added, apps.appendingPathComponent("Alpha.app")])
        try record(canvas, reloaded, context, state: "twoApps")
        context.overlay.clearToast()
        canvas.setActive(false)
        payload["states"] = states
        payload["text"] = ["widths": Array(recorder.widths.values), "metrics": Array(recorder.metrics.values)]
        payload["layerDefaults"] = recorder.layerDefaults
        let data = try JSONSerialization.data(withJSONObject: sanitize(payload), options: [.sortedKeys])
        try data.write(to: output.appendingPathComponent("app-shortcut-source.json"))
    }
}
