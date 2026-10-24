import AppKit
import AVFoundation
import QuartzCore

// Actual-source oracle of the redesigned Media Assembly (Mac 1.3.0 Sources/MediaAssemblyCanvas.swift,
// MediaAssemblyControls.swift, MediaAssemblyViewport.swift, MediaAssemblyModel.swift with the real
// MediaAssemblyController and engine) for windows/tests/media_assembly_canvas_tests.cpp: the viewport's
// new geometry (zoom(by:), dragCrop, the crop L-handles, the sticker handles and their hit test, the picture
// transform's delta / nearest), the parameter tables (ranges, defaults, clamps), and the canvas built by the
// unchanged Mac code in scripted states (dark / Light, English / Simplified Chinese / Japanese): its model
// layer tree through the design recorder (tools/hud_design_reference_support.swift), page controls,
// accessibility items, header counter, chips, the confirm requests and toasts, and the scenario's inputs so
// the Windows suite rebuilds the same state. Isolation: temporary CFFIXED_USER_HOME, a synthetic still and
// the owned silent movie copied into it, no window, no input posted (NSEvents are constructed, never sent),
// no panel, Reduce Motion on so every page installs at once and nothing animates.

private let env = HUDDesignEnvironment(renderScale: 2, reduceMotion: { true })
private func spin(_ what: String, until done: () -> Bool) {
    let limit = Date().addingTimeInterval(20)
    while !done() {
        guard Date() < limit else { fatalError("Timed out waiting for " + what) }
        RunLoop.main.run(until: Date().addingTimeInterval(0.01))
    }
}
private func row(_ canvas: MediaAssemblyCanvas, _ recorder: Recorder, state: String, language: AppLanguage, light: Bool,
                 extra: [String: Any] = [:]) throws -> [String: Any] {
    let controller = canvas.controller, stage = canvas.stageForTesting
    var row: [String: Any] = ["state": state, "language": languageName(language), "light": light,
        "layer": try recorder.tree(canvas.root, id: "root"),
        "controls": canvas.page.controls.map { controlState($0, in: canvas.root) },
        "accessibility": accessibility(canvas.accessibilityItems),
        "header": ["counter": canvas.header.counterTextForTesting],
        "tab": canvas.tab.rawValue, "tool": canvas.tool?.rawValue as Any? ?? NSNull(), "editable": canvas.isEditable,
        "chips": stage.chipTitlesForTesting, "export": stage.exportTextForTesting as Any? ?? NSNull(),
        "crop": stage.isCropVisible, "selection": stage.isSelectionVisible, "imageRect": rect(canvas.imageRect),
        "zoom": canvas.viewport.zoom, "pan": point(canvas.viewport.pan), "selected": canvas.selectedStickerID != nil,
        "document": controller.document.map { ["name": $0.name, "video": $0.isVideo, "pixels": [$0.pixelSize.width, $0.pixelSize.height], "duration": $0.duration] } as Any? ?? NSNull(),
        "adjustments": adjustments(controller.adjustments),
        "preview": controller.preview.map { [$0.width, $0.height] } as Any? ?? NSNull(),
        "rows": canvas.parameterRowsForTesting.map { ["id": $0.parameter.id, "value": $0.valueTextForTesting] }]
    if canvas.isMenuOpen { row["top"] = try recorder.tree(canvas.top, id: "top") }
    if let request = canvas.context.overlay.confirmState.request {
        row["confirm"] = ["id": request.id, "title": request.title, "message": request.message, "confirm": request.confirmTitle,
                          "danger": request.danger, "icon": request.icon?.rawValue as Any? ?? NSNull()]
    }
    if let toast = canvas.context.overlay.toastState.current { row["toast"] = ["icon": toast.icon.rawValue, "title": toast.title, "caption": toast.caption] }
    for (k, v) in extra { row[k] = v }
    return row
}
private func adjustments(_ a: MediaAssemblyAdjustments) -> [String: Any] {
    ["crop": [a.crop.x, a.crop.y, a.crop.width, a.crop.height], "turns": a.rotationQuarterTurns, "mirrored": a.mirrored, "filter": a.filter.rawValue,
     "brightness": a.brightness, "trimStart": a.trimStart, "trimEnd": a.trimEnd as Any? ?? NSNull(),
     "stickers": a.stickers.map { ["kind": $0.kind.rawValue, "x": $0.x, "y": $0.y, "size": $0.size, "rotation": $0.rotation] }]
}
private func png(width: Int, height: Int) -> Data {
    // The verification fixture (BatchSevenEightHUDVerification): teal with an orange square.
    let context = CGContext(data: nil, width: width, height: height, bitsPerComponent: 8, bytesPerRow: 0,
        space: CGColorSpaceCreateDeviceRGB(), bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
    context.setFillColor(NSColor.systemTeal.cgColor); context.fill(CGRect(x: 0, y: 0, width: width, height: height))
    context.setFillColor(NSColor.systemOrange.cgColor); context.fill(CGRect(x: 120, y: 80, width: 260, height: 260))
    return NSBitmapImageRep(cgImage: context.makeImage()!).representation(using: .png, properties: [:])!
}

@main private enum MediaAssemblyReference {
    static func main() throws {
        let home = ProcessInfo.processInfo.environment["CFFIXED_USER_HOME"]!
        precondition(CommandLine.arguments.count >= 3, "Pass a NEW output directory and the owned silent movie")
        let output = URL(fileURLWithPath: CommandLine.arguments[1]), movieFixture = URL(fileURLWithPath: CommandLine.arguments[2])
        NSApplication.shared.setActivationPolicy(.prohibited)
        var configuration = AppConfiguration.defaults
        configuration.language = .english; configuration.theme = .dark; configuration.reduceMotion = true; configuration.accentHex = "FAD41F"
        HUDRuntimeAppearance.configuration = configuration
        L10n.language = .english
        HUDColor.apply(accentHex: "FAD41F", light: false)
        let recorder = try Recorder(output: output)
        let scratch = URL(fileURLWithPath: home).appendingPathComponent("oracle", isDirectory: true)
        try FileManager.default.createDirectory(at: scratch, withIntermediateDirectories: true)
        let still = scratch.appendingPathComponent("fixture.png"), movie = scratch.appendingPathComponent("owned movie.mp4")
        try png(width: 800, height: 500).write(to: still)
        try FileManager.default.copyItem(at: movieFixture, to: movie)
        var payload: [String: Any] = ["schema": 1]

        // MARK: Viewport geometry (MediaAssemblyViewport.swift)
        var zooms: [[String: Any]] = []
        for size in [CGSize(width: 800, height: 500), CGSize(width: 500, height: 800), CGSize(width: 448, height: 412)] {
            var v = MediaAssemblyViewport()
            for (index, factor) in [1.25, 1.25, 0.8, 40, 0.05, 1.25].enumerated() {
                v.zoom(by: factor, size: size, in: MediaAssemblyStage.bounds)
                zooms.append(["size": [size.width, size.height], "step": index, "factor": factor, "zoom": v.zoom, "pan": point(v.pan)])
                v.move(by: CGPoint(x: 90, y: -70), size: size, in: MediaAssemblyStage.bounds)
                zooms.append(["size": [size.width, size.height], "step": index, "move": true, "zoom": v.zoom, "pan": point(v.pan)])
            }
        }
        var drags: [[String: Any]] = []
        for crop in [CGRect(x: 0, y: 0, width: 1, height: 1), CGRect(x: 0.2, y: 0.1, width: 0.5, height: 0.6)] {
            for corner in 0..<4 {
                for to in [CGPoint(x: 0.4, y: 0.3), CGPoint(x: -0.2, y: 1.3), CGPoint(x: 0.69, y: 0.68), CGPoint(x: 0.21, y: 0.12)] {
                    drags.append(["crop": rect(crop), "corner": corner, "to": point(to), "result": rect(MediaAssemblyViewport.dragCrop(crop, corner: corner, to: to))])
                }
            }
        }
        var origins: [[String: Any]] = []
        for (crop, image) in [(CGRect(x: 0, y: 0, width: 1, height: 1), CGRect(x: 0, y: 66, width: 448, height: 280)),
                              (CGRect(x: 0.1, y: 0.2, width: 0.5, height: 0.4), CGRect(x: -80, y: -40, width: 640, height: 500)),
                              (CGRect(x: 0.9, y: 0.9, width: 0.1, height: 0.1), CGRect(x: 30, y: 10, width: 388, height: 392))] {
            origins.append(["crop": rect(crop), "image": rect(image),
                            "origins": MediaAssemblyViewport.cropHandleOrigins(crop, image: image, stage: MediaAssemblyStage.size).map(point)])
        }
        func handleName(_ h: MediaAssemblyHandle?) -> Any {
            switch h { case .rotate?: return "rotate"; case .delete?: return "delete"; case .corner(let n)?: return "corner\(n)"; case nil: return NSNull() }
        }
        var handles: [[String: Any]] = []
        let sticker = CGRect(x: 100, y: 120, width: 80, height: 80)
        for rotation in [0.0, 37, -90, 180] {
            for p in [CGPoint(x: 140, y: 95), CGPoint(x: 193, y: 107), CGPoint(x: 99.5, y: 119.5), CGPoint(x: 186, y: 206), CGPoint(x: 140, y: 160),
                      CGPoint(x: 150, y: 92), CGPoint(x: 92, y: 200), CGPoint(x: 202, y: 99)] {
                handles.append(["rotation": rotation, "point": point(p), "handle": handleName(MediaAssemblyViewport.handle(at: p, sticker: sticker, rotation: rotation))])
            }
        }
        var transforms: [[String: Any]] = []
        var shown = MediaAssemblyGeometry(), transform = MediaAssemblyPictureTransform()
        for (turns, mirrored) in [(1, false), (2, false), (3, false), (0, false), (0, true), (1, true), (1, false), (3, true), (-1, false), (2, true), (6, false)] {
            let target = MediaAssemblyGeometry(quarterTurns: turns, mirrored: mirrored)
            let delta = MediaAssemblyPictureTransform.delta(from: shown, to: target)
            transform = transform.nearest(turns: delta.turns, reflected: delta.reflected)
            transforms.append(["turns": turns, "mirrored": mirrored, "normalized": target.quarterTurns, "delta": [delta.turns, delta.reflected ? 1 : 0],
                               "angle": transform.angle, "scale": [transform.scaleX, transform.scaleY], "swaps": transform.swapsAxes, "identity": transform.isIdentity])
            shown = target
        }
        var parameters: [[String: Any]] = []
        for tool in MediaAssemblyTool.allCases {
            for parameter in MediaAssemblyParameter.parameters(for: tool, duration: 7.3) {
                var a = MediaAssemblyAdjustments(); a.levelsWhite = 0.6; a.levelsBlack = 0.3; a.trimEnd = 4
                var writes: [[Double]] = []
                for value in [-1000.0, 0, 0.29, 0.5, 0.61, 1, 3.99, 7.3, 1000] {
                    var b = a; parameter.write(&b, value); writes.append([value, parameter.read(b)])
                }
                parameters.append(["tool": tool.rawValue, "id": parameter.id, "title": parameter.title, "minimum": parameter.range.minimum,
                                   "maximum": parameter.range.maximum, "step": parameter.range.step, "zero": parameter.range.zero as Any? ?? NSNull(),
                                   "default": parameter.defaultValue, "writes": writes, "format": parameter.format.string(1.234)])
            }
        }
        payload["viewport"] = ["zooms": zooms, "drags": drags, "origins": origins, "handles": handles, "transforms": transforms, "parameters": parameters]
        let style = HUDModuleContentStyle(dark: true, accent: .yellow, contentsScale: 2)

        // MARK: The canvas
        var rows: [[String: Any]] = []
        func session(language: AppLanguage, light: Bool) throws {
            L10n.language = language
            HUDColor.apply(accentHex: "FAD41F", light: light)
            let context = HUDDesignContext(env: env)
            let controller = MediaAssemblyController()
            let canvas = MediaAssemblyCanvas(context: context, controller: controller)
            var counter = 0
            canvas.shelfChoices = { [NotesShelfMediaChoice(id: UUID(uuidString: "00000000-0000-0000-0000-000000000001")!, title: "Shelf still.png", detail: "PNG", isSupported: true, isAvailable: true),
                                     NotesShelfMediaChoice(id: UUID(uuidString: "00000000-0000-0000-0000-000000000002")!, title: "Missing clip.mov", detail: "MOV", isSupported: true, isAvailable: false),
                                     NotesShelfMediaChoice(id: UUID(uuidString: "00000000-0000-0000-0000-000000000003")!, title: "notes.txt", detail: "TXT", isSupported: false, isAvailable: true)] }
            func add(_ state: String, _ extra: [String: Any] = [:]) throws {
                counter += 1
                rows.append(try row(canvas, recorder, state: state, language: language, light: light, extra: extra))
            }
            func settle() { canvas.settleSwap(); spin("settled page") { !canvas.isTransitioning } }
            func perform(_ id: String) { canvas.perform(id); settle() }
            _ = canvas.makeContent(for: .mediaAssembly, style: style); canvas.setActive(true)
            try add("empty")
            perform("open")
            try add("menu.open")
            perform("open:shelf")
            try add("menu.shelf")
            canvas.closePopups()
            canvas.importFiles([still])
            spin("image preview") { controller.preview != nil && canvas.isEditable && !canvas.isTransitioning }
            settle()
            try add("tiles")
            if language == .english && !light {
                perform("tool:crop"); try add("crop")
                perform("cropDone")
                perform("tool:adjust"); try add("adjust")
                perform("toolBack"); perform("tool:levels"); try add("levels")
                canvas.adjustAccessibilitySlider("parameter:black", value: 0.95)
                try add("levels.clamped")
                perform("toolReset"); perform("toolBack"); perform("tool:curves"); try add("curves")
                perform("toolBack")
                perform("mediaTab:1"); try add("filters")
                perform("filter:" + MediaAssemblyFilter.special7.rawValue); try add("filter.wuling")
                perform("mediaTab:2"); try add("stickers")
                perform("sticker:" + MediaAssemblyStickerKind.sticker29.rawValue)
                _ = canvas.keyDown(key(124)); _ = canvas.keyDown(key(124, "", flags: .option)); _ = canvas.keyDown(key(124, "", flags: [.option, .shift]))
                try add("sticker.selected")
                for _ in 0..<16 { perform("sticker:" + MediaAssemblyStickerKind.sticker1.rawValue) }
                try add("sticker.limit")
                context.overlay.clearToast()
                perform("zoomIn"); perform("zoomIn"); try add("zoomed")
                perform("zoomReset"); _ = canvas.keyDown(key(53)); try add("deselected")
                perform("mediaTab:0")
                perform("tool:rotate"); perform("tool:mirror")
                spin("turned preview") { controller.preview?.width == 500 && controller.preview?.height == 800 }
                try add("turned")
                perform("tool:crop")
                canvas.adjustAccessibilitySlider("crop.edge0", value: 0.2); canvas.adjustAccessibilitySlider("crop.edge3", value: 0.7)
                try add("turned.crop")
                perform("cropDone")
                perform("export"); try add("export.menu")
                perform("export:overwrite"); try add("confirm.overwrite")
                context.overlay.dismissConfirm()
                perform("close"); try add("confirm.close.edited")
                context.overlay.dismissConfirm()
                perform("tool:reset"); try add("confirm.reset")
                context.overlay.dismissConfirm()
                canvas.stageForTesting.setExport(0.42, title: L10n.text("Exporting", "正在导出"))
                recorder.width(L10n.text("Exporting", "正在导出") + " 100%", HUDTextStyle(size: 11, weight: .medium, color: HUDColor.accentInk, tracking: 0.06, uppercase: true, tabular: true))
                try add("stage.export")
                canvas.stageForTesting.setExport(nil, title: "")
                // Movie: VID 01, Trim, the inert Stickers grid, the playback bar and the trim sliders.
                canvas.importFiles([movie])
                spin("movie preview") { controller.document?.isVideo == true && controller.preview != nil && canvas.isEditable && !canvas.isTransitioning }
                settle()
                try add("movie")
                perform("mediaTab:2"); try add("movie.stickers")
                perform("mediaTab:0"); perform("tool:trim"); try add("trim")
                canvas.adjustAccessibilitySlider("parameter:trimStart", value: 0.2); try add("trim.edited")
                perform("toolBack")
            } else {
                perform("mediaTab:1"); try add("filters")
                perform("mediaTab:2"); try add("stickers")
                perform("mediaTab:0")
                perform("close"); try add("confirm.close")
                context.overlay.dismissConfirm()
            }
            canvas.setActive(false); canvas.willLeave()
            _ = counter
        }
        try session(language: .english, light: false)
        try session(language: .simplifiedChinese, light: false)
        try session(language: .japanese, light: false)
        try session(language: .english, light: true)
        L10n.language = .english; HUDColor.apply(accentHex: "FAD41F", light: false)
        payload["canvas"] = rows
        // The module header's title and subtitle line metrics (its baselines) in every region.
        for language in HUDTextLanguage.regions { _ = recorder.lineMetrics(HUDType.moduleTitle, language: language); _ = recorder.lineMetrics(HUDType.titleSubtitle, language: language) }
        payload["text"] = ["widths": recorder.widths.keys.sorted().map { recorder.widths[$0]! }, "metrics": recorder.metrics.keys.sorted().map { recorder.metrics[$0]! }]
        try FileManager.default.createDirectory(at: output, withIntermediateDirectories: true)
        let data = try JSONSerialization.data(withJSONObject: sanitize(payload), options: [.sortedKeys, .withoutEscapingSlashes])
        try data.write(to: output.appendingPathComponent("media-assembly-source.json"), options: .withoutOverwriting)
        print("Exported \(rows.count) canvas states, \(recorder.widths.count) measured widths")
    }
}
