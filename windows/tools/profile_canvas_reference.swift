import AppKit
import QuartzCore

// Actual-source oracle of the redesigned Personal ID (Mac 1.3.0 Sources/PersonalProfileCanvas.swift) over the
// real UserProfileStore in the temporary home, with synthetic PNG images. Exported:
//  - "rules": PersonalProfileCanvas.isValid / value(_:in:) / sliderSpec / textLimit / syncField tables;
//  - "states": the canvas (default, hovers, the ⋯ menu, the Card theme and portrait pop-overs, hidden
//    text, Recording mode, a sync-locked card, birthday shown, a custom card colour, an avatar at zoom 2.4
//    with offsets, background art with geometry, zh-Hans, Light pop-over, the failed store) as its model
//    layer tree, controls, accessibility items, header, toast, and the art box / portrait numbers, with the
//    scenario INPUT (profile values, synced fields, best score, work seconds) the Windows suite rebuilds.
// Isolation: temporary CFFIXED_USER_HOME, a store in a temporary directory, TZ UTC; no window, no input
// posted, no panel opened; Reduce Motion on so every change lands at once.

private var reduce = true
private let env = HUDDesignEnvironment(renderScale: 2, reduceMotion: { reduce })

private func png(_ url: URL, width: Int, height: Int) throws {
    let ctx = CGContext(data: nil, width: width, height: height, bitsPerComponent: 8, bytesPerRow: width * 4,
                        space: CGColorSpace(name: CGColorSpace.sRGB)!, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
    for y in 0..<height { for x in 0..<width {
        ctx.setFillColor(CGColor(srgbRed: CGFloat(x) / CGFloat(width), green: CGFloat(y) / CGFloat(height), blue: 0.5, alpha: 1))
        ctx.fill(CGRect(x: x, y: y, width: 1, height: 1))
    } }
    let rep = NSBitmapImageRep(cgImage: ctx.makeImage()!)
    try rep.representation(using: .png, properties: [:])!.write(to: url)
}
private func transformRow(_ t: CATransform3D) -> [Double] { [t.m11, t.m12, t.m21, t.m22, t.m41, t.m42].map { round6(Double($0)) } }
private func find(_ layer: CALayer, _ name: String) -> CALayer? {
    if layer.name == name { return layer }
    for child in layer.sublayers ?? [] { if let found = find(child, name) { return found } }
    return nil
}
private func profileRow(_ p: UserProfile) -> [String: Any] {
    ["name": p.name, "tag": p.tag, "uid": p.uid, "playerIDOverride": p.playerIDOverride as Any? ?? NSNull(),
     "gamePlayerID": p.gamePlayerID as Any? ?? NSNull(), "introduction": p.introduction,
     "awakeningDate": p.awakeningDate.timeIntervalSinceReferenceDate, "showsBirthday": p.showsBirthday,
     "birthdayMonth": p.birthdayMonth, "birthdayDay": p.birthdayDay, "permissionLevel": p.permissionLevel,
     "explorationLevel": p.explorationLevel, "operatorsCount": p.operatorsCount, "weaponsCount": p.weaponsCount,
     "archivesCount": p.archivesCount, "themeColorHex": p.themeColorHex as Any? ?? NSNull(),
     "avatar": p.avatarFilename != nil, "background": p.backgroundFilename != nil,
     "avatarZoom": p.avatarZoom, "avatarOffsetX": p.avatarOffsetX, "avatarOffsetY": p.avatarOffsetY,
     "backgroundWidth": p.backgroundWidth, "backgroundZoom": p.backgroundZoom, "backgroundOffsetX": p.backgroundOffsetX,
     "backgroundOffsetY": p.backgroundOffsetY, "thumbnailZoom": p.thumbnailZoom, "thumbnailOffsetX": p.thumbnailOffsetX,
     "thumbnailOffsetY": p.thumbnailOffsetY, "accumulatedWorkSeconds": p.accumulatedWorkSeconds]
}

@main private enum ProfileCanvasReference {
    static func main() throws {
        let home = ProcessInfo.processInfo.environment["CFFIXED_USER_HOME"]!
        precondition(CommandLine.arguments.count >= 2)
        let output = URL(fileURLWithPath: CommandLine.arguments[1])
        NSApplication.shared.setActivationPolicy(.prohibited)
        NSTimeZone.default = TimeZone(secondsFromGMT: 0)!
        var configuration = AppConfiguration.defaults
        configuration.language = .english; configuration.theme = .dark; configuration.reduceMotion = true; configuration.accentHex = "FAD41F"
        HUDRuntimeAppearance.configuration = configuration
        L10n.language = .english
        HUDColor.apply(accentHex: "FAD41F", light: false)
        let recorder = try Recorder(output: output)
        var payload: [String: Any] = ["schema": 1]

        // MARK: Rules (isValid, value, sliderSpec, textLimit, syncField)
        let samples: [(PersonalProfileField, [String])] = [
            (.name, ["Endmin", "", "  ", "abcdefghijklmnopqrst", "abcdefghijklmnopqrstu", "a\u{0007}b", " padded "]),
            (.tag, ["1234", "#1234", "##12", "12a", "", "#", "1234567890", "12345678901", "#1234567890", " 77 "]),
            (.playerID, ["", "abc", String(repeating: "x", count: 64), String(repeating: "x", count: 65)]),
            (.introduction, ["", "hello", String(repeating: "y", count: 150), String(repeating: "y", count: 151)]),
            (.awakeningDate, ["2025/12/31", "2025-12-31", "2025/02/29", "2024/02/29", "0/1/1", "10000/1/1", "2025/13/01", "2025/1", "+2025/1/2"]),
            (.birthday, ["02/29", "2-29", "04/31", "12/31", "13/01", "1", "0/5"]),
            (.permissionLevel, ["1", "60", "61", "0", "+5", "05", "x"]),
            (.explorationLevel, ["1", "7", "8"]),
            (.operatorsCount, ["0", "99999", "100000", "-1", "１２"])]
        payload["valid"] = samples.flatMap { field, inputs in inputs.map { ["field": field.rawValue, "input": $0, "valid": PersonalProfileCanvas.isValid(field, $0)] } }
        payload["limits"] = PersonalProfileField.allCases.map { ["field": $0.rawValue, "limit": $0.textLimit as Any? ?? NSNull(),
                                                                 "sync": $0.syncField?.rawValue as Any? ?? NSNull(), "title": $0.title] }
        payload["sliders"] = (PersonalProfileField.geometryFields + PersonalProfileField.portraitFields).map { field -> [String: Any] in
            let spec = PersonalProfileCanvas.sliderSpec(field)
            return ["field": field.rawValue, "minimum": spec.range.minimum, "maximum": spec.range.maximum, "step": spec.range.step,
                    "zero": spec.range.zero as Any? ?? NSNull(), "format": "\(spec.format)"]
        }

        // MARK: States
        let root = URL(fileURLWithPath: home).appendingPathComponent("oracle", isDirectory: true)
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        let style = HUDModuleContentStyle(dark: true, accent: configuration.accentColor, contentsScale: 2)
        var states: [[String: Any]] = []
        var best = 3262, work: TimeInterval = 7 * 3600 + 1234
        func record(_ canvas: PersonalProfileCanvas, _ context: HUDDesignContext, _ store: UserProfileStore?, state: String,
                    language: AppLanguage = .english, light: Bool = false, synced: [String]? = nil, extra: [String: Any] = [:]) throws {
            var row: [String: Any] = ["state": state, "language": languageName(language), "light": light,
                "layer": try recorder.tree(canvas.root, id: "root"),
                "controls": canvas.page.controls.map { controlState($0, in: canvas.root) },
                "accessibility": accessibility(canvas.accessibilityItems),
                "header": ["counter": canvas.header.counterTextForTesting, "title": canvas.header.titleTextForTesting.0],
                "input": ["profile": profileRow(store?.profile ?? UserProfile(awakeningDate: Date(timeIntervalSinceReferenceDate: 0), uid: "1000000000")),
                          "store": store != nil, "synced": synced as Any? ?? NSNull(), "best": best, "work": work,
                          "recording": context.recordingMode],
                "textHidden": canvas.isTextHidden, "inputLocked": canvas.isInputLocked, "presentingPanel": canvas.isPresentingPanel]
            if canvas.isMenuOpen || canvas.isPopoverOpen { row["top"] = try recorder.tree(canvas.top, id: "top") }
            if let toast = context.overlay.toastState.current { row["toast"] = ["icon": toast.icon.rawValue, "title": toast.title, "caption": toast.caption] }
            if let box = find(canvas.root, "profile.art.box"), let picture = find(canvas.root, "profile.art.image"), let host = find(canvas.root, "profile.art") {
                row["art"] = ["host": rect(host.frame), "boxPosition": point(box.position), "boxBounds": rect(box.bounds),
                              "picturePosition": point(picture.position), "pictureBounds": rect(picture.bounds),
                              "pictureTransform": transformRow(picture.transform), "mask": rect(host.mask?.frame ?? .zero)]
            }
            if let portrait = find(canvas.root, "profile.portrait") {
                row["portrait"] = ["position": point(portrait.position), "bounds": rect(portrait.bounds), "transform": transformRow(portrait.transform)]
            }
            if let silhouette = find(canvas.root, "profile.silhouette") {
                row["silhouette"] = ["frame": rect(silhouette.frame), "transform": transformRow(silhouette.transform)]
            }
            for (k, v) in extra { row[k] = v }
            states.append(row)
        }
        func show(_ canvas: PersonalProfileCanvas) { _ = canvas.makeContent(for: .profile, style: style) }
        func control(_ canvas: PersonalProfileCanvas, _ id: String) -> HUDControl { canvas.page.controls.first { $0.id == id }! }
        func hover(_ canvas: PersonalProfileCanvas, _ id: String?) {
            HUDControlHighlightLayer.update(in: canvas.root, point: id.map { centre(control(canvas, $0), in: canvas.root) }, pressed: false)
        }
        func makeStore(_ name: String, _ edit: (inout UserProfile) -> Void) throws -> UserProfileStore {
            let directory = root.appendingPathComponent(name, isDirectory: true)
            let store = try UserProfileStore(directory: directory, now: Date(timeIntervalSinceReferenceDate: 0))
            try store.update { p in
                p.name = "Endministrator"; p.tag = "1018"; p.introduction = "Talos-II field engineer. Prefers tea."
                p.awakeningDate = Date(timeIntervalSinceReferenceDate: 788_961_600) // 2026/01/01 12:00 UTC
                p.birthdayMonth = 3; p.birthdayDay = 14; p.permissionLevel = 42; p.explorationLevel = 5
                p.operatorsCount = 23; p.weaponsCount = 41; p.archivesCount = 108; p.accumulatedWorkSeconds = 3600
                edit(&p)
            }
            return store
        }
        func canvasFor(_ store: UserProfileStore?, error: String? = nil) -> (PersonalProfileCanvas, HUDDesignContext) {
            let context = HUDDesignContext(env: env)
            let canvas = PersonalProfileCanvas(context: context, store: store, error: error, workSeconds: { work }, minigameBest: { best })
            show(canvas); canvas.setActive(true)
            return (canvas, context)
        }

        // Default card (no images: the silhouette), then hovers and the ⋯ menu.
        do {
            let store = try makeStore("Default") { _ in }
            let (canvas, context) = canvasFor(store)
            try record(canvas, context, store, state: "default")
            hover(canvas, "profile:name"); try record(canvas, context, store, state: "hoverName")
            hover(canvas, "profile:menu"); try record(canvas, context, store, state: "hoverMore")
            hover(canvas, "profile:visibility"); try record(canvas, context, store, state: "hoverEye")
            hover(canvas, "profile:backgroundMenu"); try record(canvas, context, store, state: "hoverTheme")
            hover(canvas, nil)
            control(canvas, "profile:menu").onActivate?()
            try record(canvas, context, store, state: "moreMenu")
            canvas.closeMenu()
            control(canvas, "profile:backgroundMenu").onActivate?()
            try record(canvas, context, store, state: "themePopover")
            canvas.closePopover()
            // The portrait pop-over (Adjust portrait from the ⋯ menu).
            control(canvas, "profile:menu").onActivate?()
            canvas.performAccessibilityAction("profile.more:portrait")
            try record(canvas, context, store, state: "portraitPopover")
            canvas.closePopover()
            control(canvas, "profile:visibility").onActivate?()
            try record(canvas, context, store, state: "textHidden")
            control(canvas, "profile:visibility").onActivate?()
            context.setRecordingMode(true)
            try record(canvas, context, store, state: "recording")
            context.setRecordingMode(false)
            control(canvas, "profile:toggleDateLabel").onActivate?()
            try record(canvas, context, store, state: "birthday")
            control(canvas, "profile:toggleDateLabel").onActivate?()
            canvas.gameSyncActive = true; canvas.syncedFields = [.name, .levels]
            try record(canvas, context, store, state: "synced", synced: ["name", "levels"])
            canvas.syncedFields = nil
            try record(canvas, context, store, state: "syncedAll", synced: ["name", "tag", "uid", "awakening", "levels", "counts"])
            canvas.gameSyncActive = false; canvas.syncedFields = nil
            try store.update { $0.themeColorHex = "6EDFE8" }
            try record(canvas, context, store, state: "customColor")
            control(canvas, "profile:backgroundMenu").onActivate?()
            try record(canvas, context, store, state: "themePopoverColor")
            canvas.closePopover()
            try store.update { $0.themeColorHex = nil }
            best = 0; work = 0
            canvas.setActive(false); canvas.setActive(true)
            try record(canvas, context, store, state: "noBest")
            best = 3262; work = 7 * 3600 + 1234
            canvas.setActive(false)
            L10n.language = .simplifiedChinese
            show(canvas); canvas.setActive(true)
            try record(canvas, context, store, state: "default", language: .simplifiedChinese)
            control(canvas, "profile:backgroundMenu").onActivate?()
            try record(canvas, context, store, state: "themePopover", language: .simplifiedChinese)
            canvas.closePopover(); canvas.setActive(false)
            L10n.language = .japanese
            show(canvas); canvas.setActive(true)
            try record(canvas, context, store, state: "default", language: .japanese)
            canvas.setActive(false)
            L10n.language = .english
            HUDColor.apply(accentHex: "FAD41F", light: true); context.refreshPalette(); show(canvas); canvas.setActive(true)
            control(canvas, "profile:backgroundMenu").onActivate?()
            try record(canvas, context, store, state: "themePopover", light: true)
            canvas.closePopover(); canvas.setActive(false)
            HUDColor.apply(accentHex: "FAD41F", light: false); context.refreshPalette()
        }
        // Images: an 120 x 80 avatar at zoom 2.4 with offsets, and a 400 x 300 background.
        do {
            let avatarURL = root.appendingPathComponent("avatar.png"), backgroundURL = root.appendingPathComponent("background.png")
            try png(avatarURL, width: 120, height: 80); try png(backgroundURL, width: 400, height: 300)
            let store = try makeStore("Images") { _ in }
            try store.importImage(from: avatarURL, kind: .avatar)
            try store.importImage(from: backgroundURL, kind: .background)
            try store.update { p in
                p.avatarZoom = 2.4; p.avatarOffsetX = 0.3; p.avatarOffsetY = -0.5
                p.backgroundWidth = 720; p.backgroundZoom = 1.7; p.backgroundOffsetX = -60; p.backgroundOffsetY = 40
            }
            let (canvas, context) = canvasFor(store)
            try record(canvas, context, store, state: "images")
            try store.update { $0.avatarZoom = 1; $0.avatarOffsetX = 0; $0.avatarOffsetY = 0; $0.backgroundZoom = 1 }
            try record(canvas, context, store, state: "imagesRest")
            control(canvas, "profile:visibility").onActivate?()
            try record(canvas, context, store, state: "imagesHidden")
            canvas.setActive(false)
        }
        // The silhouette at zoom 3.5 with offsets.
        do {
            let store = try makeStore("Silhouette") { p in p.avatarZoom = 3.5; p.avatarOffsetX = -1; p.avatarOffsetY = 0.25 }
            let (canvas, context) = canvasFor(store)
            try record(canvas, context, store, state: "silhouetteZoom")
            canvas.setActive(false)
        }
        // A failed store (the canvas's nil store): defaults, disabled editing, the Notice toast.
        do {
            let (canvas, context) = canvasFor(nil, error: "The personal card could not be read. The saved data has been preserved.")
            try record(canvas, context, nil, state: "unavailable")
            canvas.setActive(false)
        }
        payload["states"] = states
        payload["text"] = ["widths": Array(recorder.widths.values), "metrics": Array(recorder.metrics.values)]
        payload["layerDefaults"] = recorder.layerDefaults
        let data = try JSONSerialization.data(withJSONObject: sanitize(payload), options: [.sortedKeys])
        try data.write(to: output.appendingPathComponent("profile-canvas-source.json"))
    }
}
