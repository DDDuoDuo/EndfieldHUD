import AppKit
import QuartzCore
import UniformTypeIdentifiers

// Actual-source oracle of the redesigned File Shelf (Mac 1.3.0 Sources/FileShelfCanvas.swift with its real
// FileShelfStore over synthetic files). The unchanged Mac canvas is built in scripted states (dark / Light,
// English / Simplified Chinese, empty, the 4-column grid, hover, the properties card of a file and of a folder,
// a multi-selection, a scrolled grid, the context menu, an unavailable reference, Recording mode, the drop
// target) and exported as its model layer tree with the design recorder (tools/hud_design_reference_support.swift),
// its page controls and accessibility items, and the scenario's INPUT (the store's items, the selection, the
// properties the card read), so the Windows suite rebuilds the same state. Also exported: the selection rules
// (click / modifier click / double-click / arrows / Space / empty-space press) as id sequences, and the
// formatting functions (tileKind, extensionLabel, kindText, location, sizeTag) over synthetic items, with the
// UTType kind of every extension the Windows table lists.
// Isolation: temporary CFFIXED_USER_HOME (the store and every file live there), no window, no input posted
// (NSEvents are constructed, never sent), Reduce Motion on so every page installs at once, TZ=UTC.

private var reduce = true
private let env = HUDDesignEnvironment(renderScale: 2, reduceMotion: { reduce })

private func pump() { RunLoop.main.run(until: Date().addingTimeInterval(0.25)) }

@main private enum FileShelfReference {
    static func main() throws {
        let home = ProcessInfo.processInfo.environment["CFFIXED_USER_HOME"]!
        precondition(CommandLine.arguments.count >= 2)
        let output = URL(fileURLWithPath: CommandLine.arguments[1])
        NSApplication.shared.setActivationPolicy(.prohibited)
        var configuration = AppConfiguration.defaults
        configuration.language = .english; configuration.theme = .dark; configuration.reduceMotion = true
        configuration.accentHex = "FAD41F"
        HUDRuntimeAppearance.configuration = configuration
        L10n.language = .english
        HUDColor.apply(accentHex: "FAD41F", light: false)
        let recorder = try Recorder(output: output)
        var payload: [String: Any] = ["schema": 1, "home": NSHomeDirectory()]
        let style = HUDModuleContentStyle(dark: true, accent: .yellow, contentsScale: 2)
        func themed(_ light: Bool) { HUDColor.apply(accentHex: "FAD41F", light: light) }

        // Synthetic originals (sizes chosen for every ByteCountFormatter unit the tiles show).
        let files = URL(fileURLWithPath: home).appendingPathComponent("Documents/Talos", isDirectory: true)
        try FileManager.default.createDirectory(at: files, withIntermediateDirectories: true)
        func file(_ name: String, _ bytes: Int) -> URL {
            let url = files.appendingPathComponent(name)
            FileManager.default.createFile(atPath: url.path, contents: Data(count: bytes)); return url
        }
        let folder = files.appendingPathComponent("Operations", isDirectory: true)
        try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        for name in ["a.txt", "b.txt", "c.txt", ".hidden"] { FileManager.default.createFile(atPath: folder.appendingPathComponent(name).path, contents: Data(count: 4)) }
        let urls = [file("Field Report.pdf", 1_234), file("Talos Survey.png", 48_200), file("Patrol Footage.mp4", 2_412_345),
                    file("Archive Bundle.zip", 512), file("Notes.txt", 16), file("Readme", 100), folder,
                    file("Supply Manifest.csv", 9_999), file("Signal Log 01.txt", 1_000), file("Signal Log 02.txt", 999_500),
                    file("Signal Log 03.txt", 1_050_000), file("Map Tiles.tar", 12_000_000), file("Voice Memo.m4a", 345_678),
                    file("Draft Plan.key", 7_777)]
        let store = try FileShelfStore(directory: URL(fileURLWithPath: home).appendingPathComponent("FileShelf", isDirectory: true))
        try store.add(urls: urls)
        let context = HUDDesignContext(env: env)
        let canvas = FileShelfCanvas(context: context, store: store)
        func show() { _ = canvas.makeContent(for: .fileShelf, style: style) }
        func items() -> [[String: Any]] {
            store.items.map { item -> [String: Any] in
                var row: [String: Any] = ["id": item.id.uuidString, "name": item.name, "lastKnownPath": item.lastKnownPath,
                                          "typeDescription": item.typeDescription, "isDirectory": item.isDirectory]
                if let bytes = item.byteCount { row["byteCount"] = bytes }
                if let error = item.availabilityError { row["availabilityError"] = error }
                return row
            }
        }
        let calendar: Calendar = { var c = Calendar(identifier: .gregorian); c.timeZone = TimeZone.current; return c }()
        func local(_ date: Date?) -> Any {
            guard let date else { return NSNull() }
            let c = calendar.dateComponents([.year, .month, .day, .hour, .minute], from: date)
            return [c.year!, c.month!, c.day!, c.hour!, c.minute!]
        }
        func input() -> [String: Any] {
            var properties: [String: Any] = [:]
            for (id, value) in canvas.propertiesForTesting {
                properties[id.uuidString] = ["path": value.path, "created": local(value.created), "modified": local(value.modified),
                                             "itemCount": value.itemCount as Any? ?? NSNull()]
            }
            return ["items": items(), "selected": store.items.filter { canvas.selectedIDs.contains($0.id) }.map(\.id.uuidString),
                    "focus": canvas.focusID?.uuidString as Any? ?? NSNull(), "offset": canvas.scrollOffset, "properties": properties,
                    "recording": context.recordingMode]
        }
        func row(_ state: String, language: AppLanguage = .english, light: Bool = false) throws -> [String: Any] {
            var row: [String: Any] = ["state": state, "language": languageName(language), "light": light,
                "layer": try recorder.tree(canvas.root, id: "root"),
                "controls": canvas.page.controls.map { controlState($0, in: canvas.root) },
                "accessibility": accessibility(canvas.accessibilityItems), "input": input(),
                "header": ["counter": canvas.header.counterTextForTesting, "title": canvas.header.titleTextForTesting.0],
                "columns": canvas.columnsForTesting as Any? ?? NSNull(), "builtTiles": canvas.builtTileCountForTesting,
                "card": canvas.hasPropertiesCardForTesting]
            if canvas.isMenuOpen { row["top"] = try recorder.tree(canvas.top, id: "top") }
            return row
        }
        func id(_ index: Int) -> UUID { store.items[index].id }
        var rows: [[String: Any]] = []

        // The grid (4 columns over 14 items: it scrolls, so the tiles leave the 4 px bar column).
        show(); canvas.setActive(true)
        rows.append(try row("grid"))
        let tile = canvas.page.controls.first { $0.id == "file:2" }!
        HUDControlHighlightLayer.update(in: canvas.root, point: centre(tile, in: canvas.root), pressed: false)
        rows.append(try row("hoverTile"))
        HUDControlHighlightLayer.update(in: canvas.root, point: nil, pressed: false)
        // The properties card of a file, then of a folder (the reads arrive on the main queue).
        canvas.click(id(0)); pump()
        rows.append(try row("cardFile"))
        canvas.click(id(6)); pump()
        rows.append(try row("cardFolder"))
        canvas.click(id(1), modifiers: .shift)
        rows.append(try row("multi"))
        _ = canvas.scroll(at: CGPoint(x: 200, y: 300), delta: CGPoint(x: 0, y: 260))
        rows.append(try row("scrolled"))
        _ = canvas.scroll(at: CGPoint(x: 200, y: 300), delta: CGPoint(x: 0, y: -1000))
        _ = canvas.rightPointerDown(at: CGPoint(x: 220, y: 81 + 5 + 60))
        rows.append(try row("contextMenu"))
        canvas.closeMenu()
        canvas.setDropTarget(true)
        rows.append(try row("dropTarget"))
        canvas.setDropTarget(false)
        canvas.select([id(2)]); pump()
        context.setRecordingMode(true)
        rows.append(try row("recordingCard"))
        context.setRecordingMode(false)
        rows.append(try row("cardVideo"))
        L10n.language = .simplifiedChinese; canvas.setActive(false); show(); canvas.setActive(true)
        rows.append(try row("cardVideo", language: .simplifiedChinese))
        L10n.language = .english
        themed(true); context.refreshPalette(); show(); pump()
        rows.append(try row("cardVideo", light: true))
        themed(false); context.refreshPalette(); show()
        // An unavailable reference (its original deleted): selectable, no size, Open and Reveal disabled.
        canvas.setActive(false)
        try FileManager.default.removeItem(at: urls[4])
        show(); canvas.setActive(true); canvas.select([id(4)]); pump()
        rows.append(try row("unavailable"))

        // Selection rules (:392-443, :616-660) as id index sequences after each step.
        func indices(_ ids: Set<UUID>) -> [Int] { store.items.enumerated().filter { ids.contains($0.element.id) }.map(\.offset) }
        var rules: [[String: Any]] = []
        func step(_ name: String, _ action: () -> Void) {
            action()
            rules.append(["step": name, "selected": indices(canvas.selectedIDs),
                          "focus": canvas.focusID.flatMap { f in store.items.firstIndex { $0.id == f } } as Any? ?? NSNull(),
                          "columns": canvas.columnsForTesting as Any? ?? NSNull(), "card": canvas.hasPropertiesCardForTesting])
        }
        canvas.select([])
        step("click 3") { canvas.click(id(3)) }
        step("click 3 again") { canvas.click(id(3)) }
        step("click 3") { canvas.click(id(3)) }
        step("shift-click 5") { canvas.click(id(5), modifiers: .shift) }
        step("control-click 7") { canvas.click(id(7), modifiers: .control) }
        step("command-click 5") { canvas.click(id(5), modifiers: .command) }
        step("click 7") { canvas.click(id(7)) }
        step("double-click 7") { canvas.click(id(7), clickCount: 2) }
        step("press empty space") { _ = canvas.pointerDown(at: CGPoint(x: 129, y: 300)) }  // the gap between columns 1 and 2
        step("press header") { canvas.click(id(1)); _ = canvas.pointerDown(at: CGPoint(x: 300, y: 40)) }
        step("right arrow") { _ = canvas.keyDown(key(124)) }
        step("down arrow (3 columns beside the card)") { _ = canvas.keyDown(key(125)) }
        step("shift down") { _ = canvas.keyDown(key(125, flags: .shift)) }
        step("shift left") { _ = canvas.keyDown(key(123, flags: .shift)) }
        step("space closes nothing: several selected") { _ = canvas.keyDown(key(49, " ")) }
        step("space closes the card") { _ = canvas.keyDown(key(49, " ")) }
        step("space opens the focused tile") { _ = canvas.keyDown(key(49, " ")) }
        step("up arrow") { _ = canvas.keyDown(key(126)) }
        step("left arrow at the start") { canvas.select([]); _ = canvas.keyDown(key(123)) }
        payload["rules"] = rules
        // The empty shelf (English: the bracketed title alone; Simplified Chinese adds its line).
        try store.clear(); canvas.setActive(false); show(); canvas.setActive(true)
        rows.append(try row("empty"))
        L10n.language = .simplifiedChinese; canvas.setActive(false); show(); canvas.setActive(true)
        rows.append(try row("empty", language: .simplifiedChinese))
        L10n.language = .english
        payload["shelf"] = rows

        // Formatting over synthetic items (no bookmark is resolved).
        func synthetic(_ path: String, directory: Bool = false, bytes: Int64? = nil, error: String? = nil, type: String = "Document") -> ShelfItem {
            ShelfItem(bookmark: Data(), isSecurityScoped: false, lastKnownPath: path, name: (path as NSString).lastPathComponent,
                      typeDescription: type, byteCount: bytes, isDirectory: directory, availabilityError: error)
        }
        let samples = [synthetic("\(NSHomeDirectory())/Desktop/Report.PDF", bytes: 2_412_345), synthetic("/Volumes/Data/clip.mov", bytes: 0),
                       synthetic("/tmp/archive.tar.gz", bytes: 1), synthetic("/tmp/no-extension", bytes: 999), synthetic("/tmp/.bashrc", bytes: 12),
                       synthetic("/tmp/trailing.", bytes: 5), synthetic(NSHomeDirectory() + "/Folder", directory: true),
                       synthetic(NSHomeDirectory() + "/file.txt", bytes: 48_200_000), synthetic("/file.txt", bytes: 1000),
                       synthetic("/tmp/gone.png", bytes: 10, error: "missing"), synthetic("/tmp/plain", directory: false, bytes: 3, type: "")]
        let formats: [[String: Any]] = samples.map { item in
            ["path": item.lastKnownPath, "isDirectory": item.isDirectory, "byteCount": item.byteCount as Any? ?? NSNull(),
             "typeDescription": item.typeDescription, "availabilityError": item.availabilityError as Any? ?? NSNull(),
             "kind": FileShelfCanvas.tileKind(item).rawValue, "extensionLabel": FileShelfCanvas.extensionLabel(item),
             "kindText": FileShelfCanvas.kindText(item), "location": FileShelfCanvas.location(item),
             "sizeTag": FileShelfCanvas.sizeTag(item) as Any? ?? NSNull()]
        }
        payload["formats"] = formats
        // UTType kinds of the extensions the Windows table names (and a few it leaves as .txt).
        let extensions = "pdf png jpg jpeg heic svg psd webp tiff raw mp4 mov mkv avi mp3 wav flac m4a ogg opus webm ts zip rar 7z tar gz tgz bz2 xz dmg iso img msi pkg deb jar docx xlsx pptx txt md key pages numbers exe app lnk vhd zst".split(separator: " ")
        payload["kinds"] = extensions.map { ext -> [String: Any] in
            let item = synthetic("/tmp/sample.\(ext)", bytes: 1)
            return ["extension": String(ext), "kind": FileShelfCanvas.tileKind(item).rawValue]
        }
        payload["text"] = ["widths": Array(recorder.widths.values), "metrics": Array(recorder.metrics.values)]
        payload["layerDefaults"] = recorder.layerDefaults
        let data = try JSONSerialization.data(withJSONObject: sanitize(payload), options: [.sortedKeys])
        try data.write(to: output.appendingPathComponent("file-shelf-source.json"))
        print("Exported \(rows.count) File Shelf scenes and \(rules.count) selection steps")
    }
}
