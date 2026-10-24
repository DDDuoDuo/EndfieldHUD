import AppKit
import CoreText
import QuartzCore

// Actual-source oracle of the redesigned E-Reader and Projection pages (Mac 1.3.0 Sources/ReaderCanvas.swift with the
// real ReaderController / ReaderStore / ReaderDocument, and Sources/ProjectionCanvas.swift with ProjectionSettings and a
// session ProjectionModel). A synthetic library (three TXT books and a three-page PDF made here, one TXT deleted after its
// bookmark was made so it is MISSING) is written as library.json into a temporary CFFIXED_USER_HOME with fixed book UUIDs
// and read by an unchanged ReaderStore; the canvas is built by the unchanged Mac code in scripted states (empty library,
// Open / Library / Reading / Marks, the book menu and remove confirmation, the column and paged reading, the settings
// pop-over, bookmarks, the PDF, dark / Light, English / Simplified Chinese) and exported as its model layer tree with the
// design recorder (tools/hud_design_reference_support.swift), its controls, accessibility items, the toast / confirm it
// raised, its page layers and the scenario's INPUT (the books, the missing set, every page the controller rendered, the
// bookmark ids), so the Windows suite (tests/reader_projection_canvas_tests.cpp) rebuilds the same state over its own
// ReaderState with a provider that returns exactly these pages. The Projection page runs on a throwaway UserDefaults
// suite. Also exported: library.json after scripted ReaderStore steps (the `reading` key).
// Isolation: temporary CFFIXED_USER_HOME, TZ=UTC, no window (no file panel can open: the canvas has no host view), no
// input posted (NSEvents are constructed, never sent), Reduce Motion on so every page installs at once.

private var reduce = true
private let env = HUDDesignEnvironment(renderScale: 2, reduceMotion: { reduce })

private func bookID(_ n: Int) -> UUID { UUID(uuidString: String(format: "A0000000-0000-4000-8000-%012d", n))! }
private func spin(_ seconds: Double = 10, until done: () -> Bool) {
    let deadline = Date().addingTimeInterval(seconds)
    while !done(), Date() < deadline { RunLoop.main.run(until: Date().addingTimeInterval(0.01)) }
    precondition(done(), "oracle wait timed out")
}
private func settle() { RunLoop.main.run(until: Date().addingTimeInterval(0.15)) }
private func locationJSON(_ l: ReaderLocation?) -> Any { l.map { [$0.section, $0.block, $0.character] } ?? NSNull() }
private func pageJSON(_ p: ReaderPage) -> [String: Any] {
    ["location": locationJSON(p.location), "next": locationJSON(p.next), "previous": locationJSON(p.previous), "progress": p.progress,
     "summary": p.summary, "illustration": p.isIllustration, "pageCount": p.pageCount as Any? ?? NSNull(),
     "textHeight": p.textHeight.map { Double($0) } as Any? ?? NSNull()]
}
private func paragraphs(_ seed: Int, count: Int) -> String {
    let words = ["operator", "survey", "relay", "station", "signal", "archive", "convoy", "ridge", "beacon", "valley", "engine",
                 "harbor", "protocol", "outpost", "bridge", "current", "lantern", "meridian", "quarry", "orbit"]
    var out: [String] = []
    for p in 0..<count {
        var sentence: [String] = []
        for w in 0..<(40 + (p * 7 + seed) % 30) { sentence.append(words[(p * 13 + w * 7 + seed) % words.count]) }
        out.append("Section \(p + 1). " + sentence.joined(separator: " ").capitalized + ".")
    }
    return out.joined(separator: "\n\n")
}
private func makePDF(_ url: URL, pages: Int) {
    var box = CGRect(x: 0, y: 0, width: 420, height: 595)
    let context = CGContext(url as CFURL, mediaBox: &box, nil)!
    for page in 0..<pages {
        context.beginPDFPage(nil)
        context.setFillColor(CGColor(gray: 1, alpha: 1)); context.fill(box)
        let line = CTLineCreateWithAttributedString(NSAttributedString(string: "Survey page \(page + 1)",
            attributes: [.font: CTFontCreateWithName("Helvetica" as CFString, 24, nil)]))
        context.textPosition = CGPoint(x: 40, y: 520); CTLineDraw(line, context)
        context.endPDFPage()
    }
    context.closePDF()
}

@main private enum ReaderProjectionReference {
    static func main() throws {
        let home = ProcessInfo.processInfo.environment["CFFIXED_USER_HOME"]!
        precondition(CommandLine.arguments.count >= 2 && ProcessInfo.processInfo.environment["TZ"] == "UTC")
        let output = URL(fileURLWithPath: CommandLine.arguments[1])
        NSApplication.shared.setActivationPolicy(.prohibited)
        var configuration = AppConfiguration.defaults
        configuration.language = .english; configuration.theme = .dark; configuration.reduceMotion = true
        configuration.accentHex = "FAD41F"
        HUDRuntimeAppearance.configuration = configuration
        L10n.language = .english
        HUDColor.apply(accentHex: "FAD41F", light: false)
        let recorder = try Recorder(output: output)
        let scratch = URL(fileURLWithPath: home).appendingPathComponent("oracle", isDirectory: true)
        let books = scratch.appendingPathComponent("Books", isDirectory: true)
        try FileManager.default.createDirectory(at: books, withIntermediateDirectories: true)
        var payload: [String: Any] = ["schema": 1]
        let style = HUDModuleContentStyle(dark: true, accent: .yellow, contentsScale: 2)
        func themed(_ light: Bool) { HUDColor.apply(accentHex: "FAD41F", light: light) }

        // MARK: The synthetic library (fixed UUIDs, bookmarks made by the system for these synthetic files)
        struct Spec { let n: Int; let file: String; let title: String; let kind: String }
        let specs = [Spec(n: 1, file: "Field Manual.txt", title: "Field Manual", kind: "txt"),
                     Spec(n: 2, file: "Survey Report.pdf", title: "Survey Report", kind: "pdf"),
                     Spec(n: 3, file: "Missing Notes.txt", title: "Missing Notes", kind: "txt"),
                     Spec(n: 4, file: "Logbook.txt", title: "Rhodes Island Logbook of the Ninth Survey Expedition Across the Wastes", kind: "txt")]
        var bookRows: [[String: Any]] = [], archiveBooks: [[String: Any]] = []
        for spec in specs {
            let url = books.appendingPathComponent(spec.file)
            if spec.kind == "pdf" { makePDF(url, pages: 3) }
            else { try paragraphs(spec.n, count: spec.n == 1 ? 60 : 12).write(to: url, atomically: true, encoding: .utf8) }
            let bookmark = try url.bookmarkData(options: [], includingResourceValuesForKeys: nil, relativeTo: nil)
            archiveBooks.append(["id": bookID(spec.n).uuidString, "bookmark": bookmark.base64EncodedString(), "scoped": false,
                                 "path": url.path, "title": spec.title, "location": ["section": 0, "block": 0, "character": 0],
                                 "progress": 0, "bookmarks": [] as [Any]])
            bookRows.append(["id": bookID(spec.n).uuidString, "file": spec.file, "title": spec.title])
        }
        try FileManager.default.removeItem(at: books.appendingPathComponent("Missing Notes.txt"))
        let preferences: [String: Any] = ["fontName": "Georgia", "fontSize": 10, "lineSpacing": 2, "margin": 16, "rightToLeft": false, "continuous": true]
        func writeLibrary(_ directory: URL, books: [[String: Any]]) throws {
            try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
            let archive: [String: Any] = ["version": 1, "books": books, "preferences": preferences]
            try JSONSerialization.data(withJSONObject: archive, options: [.sortedKeys]).write(to: directory.appendingPathComponent("library.json"))
        }

        // MARK: Reader scenes
        var rows: [[String: Any]] = []
        var rendered: [String: [String: Any]] = [:]
        func observe(_ controller: ReaderController) {
            for page in controller.pages + (controller.current.map { [$0] } ?? []) {
                let book = controller.book?.id.uuidString ?? ""
                rendered["\(book),\(page.location.section),\(page.location.block),\(page.location.character),\(controller.preferences.fontSize),\(controller.preferences.vertical)"] =
                    pageJSON(page).merging(["book": book, "fontSize": controller.preferences.fontSize, "vertical": controller.preferences.vertical]) { a, _ in a }
            }
        }
        func pageLayers(_ canvas: ReaderCanvas) -> [[String: Any]] {
            var out: [[String: Any]] = []
            func walk(_ layer: CALayer) {
                if layer.name == "reader.page" {
                    out.append(["frame": rect(layer.frame), "contentsRect": rect(layer.contentsRect), "hidden": layer.isHidden,
                                "zPosition": Double(layer.zPosition), "superlayer": layer.superlayer?.name ?? ""])
                }
                layer.sublayers?.forEach(walk)
            }
            walk(canvas.root)
            return out
        }
        func capture(_ canvas: ReaderCanvas, _ context: HUDDesignContext, _ controller: ReaderController, library: String, state: String,
                     language: AppLanguage = .english, light: Bool = false, extra: [String: Any] = [:]) throws {
            observe(controller)
            var row: [String: Any] = ["module": "reader", "library": library, "state": state, "language": languageName(language), "light": light,
                "layer": try recorder.tree(canvas.root, id: "root"),
                "controls": (canvas.header.controls + canvas.page.controls).map { controlState($0, in: canvas.root) },
                "accessibility": accessibility(canvas.accessibilityItems),
                "header": ["counter": canvas.header.counterTextForTesting, "title": canvas.header.titleTextForTesting.0],
                "tab": canvas.tab.rawValue, "missing": canvas.missing.map(\.uuidString).sorted(),
                "current": controller.current.map(pageJSON) as Any? ?? NSNull(), "pageLayers": pageLayers(canvas),
                "preferences": ["fontSize": controller.preferences.fontSize, "vertical": controller.preferences.vertical,
                                "lineSpacing": controller.preferences.lineSpacing, "margin": controller.preferences.margin,
                                "fontName": controller.preferences.fontName],
                "bookmarks": controller.book?.bookmarks.map { ["id": $0.id.uuidString, "location": locationJSON($0.location), "progress": $0.progress] } ?? [],
                "selected": controller.book?.id.uuidString as Any? ?? NSNull()]
            if canvas.isMenuOpen || canvas.isPopoverOpen { row["top"] = try recorder.tree(canvas.top, id: "top") }
            row["toast"] = context.overlay.toastState.current.map { ["title": $0.title, "caption": $0.caption] } ?? NSNull()
            row["confirm"] = context.overlay.confirmState.request.map { ["id": $0.id, "title": $0.title, "message": $0.message,
                                                                          "confirm": $0.confirmTitle, "danger": $0.danger] } ?? NSNull()
            for (k, v) in extra { row[k] = v }
            rows.append(row)
        }
        func item(_ canvas: HUDDesignCanvas, label: String) -> String {
            guard let found = canvas.accessibilityItems.first(where: { $0.label == label }) else { fatalError("no item \(label)") }
            return found.id
        }
        func control(_ canvas: HUDDesignCanvas, _ id: String) -> HUDControl {
            guard let found = (canvas.header.controls + canvas.page.controls).first(where: { $0.id == id }) else {
                fatalError("no control \(id) in \(canvas.page.controls.map(\.id))")
            }
            return found
        }
        let shelf = [NotesShelfMediaChoice(id: bookID(90), title: "Briefing.pdf", detail: "PDF", isSupported: true, isAvailable: true),
                     NotesShelfMediaChoice(id: bookID(91), title: "Clip.mov", detail: "MOV", isSupported: false, isAvailable: true),
                     NotesShelfMediaChoice(id: bookID(92), title: "Offline.epub", detail: "EPUB", isSupported: true, isAvailable: false)]
        var shelfShown: [NotesShelfMediaChoice] = []

        do { // An empty library.
            let directory = scratch.appendingPathComponent("EmptyReader", isDirectory: true)
            try writeLibrary(directory, books: [])
            let controller = ReaderController(store: try ReaderStore(directory: directory))
            let context = HUDDesignContext(env: env)
            let canvas = ReaderCanvas(context: context, controller: controller)
            canvas.shelfChoices = { shelfShown }
            func show() { _ = canvas.makeContent(for: .reader, style: style) }
            show(); canvas.setActive(true); settle()
            try capture(canvas, context, controller, library: "empty", state: "library")
            canvas.page.tabs!.tabs[0].activate()
            try capture(canvas, context, controller, library: "empty", state: "open")
            shelfShown = shelf
            canvas.page.tabs!.tabs[1].activate(); canvas.page.tabs!.tabs[0].activate()
            try capture(canvas, context, controller, library: "empty", state: "openShelf")
            control(canvas, "fromShelf").activate()
            try capture(canvas, context, controller, library: "empty", state: "shelfMenu")
            canvas.closeMenu()
            canvas.page.tabs!.tabs[2].activate()
            try capture(canvas, context, controller, library: "empty", state: "reading")
            canvas.page.tabs!.tabs[3].activate()
            try capture(canvas, context, controller, library: "empty", state: "marks")
            L10n.language = .simplifiedChinese; canvas.setActive(false); show(); canvas.setActive(true); settle()
            canvas.page.tabs!.tabs[0].activate()
            try capture(canvas, context, controller, library: "empty", state: "openShelf", language: .simplifiedChinese)
            L10n.language = .english
            canvas.setActive(false)
            shelfShown = []
        }

        var bookmarkIDs: [String] = []
        do { // Four books, one missing.
            let directory = scratch.appendingPathComponent("Reader", isDirectory: true)
            try writeLibrary(directory, books: archiveBooks)
            let controller = ReaderController(store: try ReaderStore(directory: directory))
            let context = HUDDesignContext(env: env)
            let canvas = ReaderCanvas(context: context, controller: controller)
            canvas.shelfChoices = { [] }
            let changed = controller.onChange
            controller.onChange = { observe(controller); changed?() }
            func show() { _ = canvas.makeContent(for: .reader, style: style) }
            func tile(_ n: Int) -> HUDControl { control(canvas, "book:" + bookID(n).uuidString) }
            show(); canvas.setActive(true)
            spin { canvas.missing == [bookID(3)] }
            try capture(canvas, context, controller, library: "books", state: "library")
            HUDControlHighlightLayer.update(in: canvas.root, point: centre(tile(1), in: canvas.root), pressed: false)
            try capture(canvas, context, controller, library: "books", state: "hoverTile")
            HUDControlHighlightLayer.update(in: canvas.root, point: nil, pressed: false)
            control(canvas, "book:" + bookID(1).uuidString + ":more").activate()
            try capture(canvas, context, controller, library: "books", state: "bookMenu")
            for code: UInt16 in [125, 125, 125, 36] { _ = canvas.keyDown(key(code)) }
            try capture(canvas, context, controller, library: "books", state: "removeConfirm")
            context.overlay.dismissConfirm(); context.overlay.reset()
            // Opening a book: the column (top to bottom is the default direction).
            tile(1).activate()
            spin { controller.current != nil && controller.book?.id == bookID(1) && canvas.page.controls.contains { $0.id == "bookmark" } }
            settle()
            try capture(canvas, context, controller, library: "books", state: "reading")
            control(canvas, "bookmark").activate()
            spin { controller.book?.bookmarks.count == 1 && context.overlay.toastState.current != nil }
            bookmarkIDs.append(controller.book!.bookmarks[0].id.uuidString)
            try capture(canvas, context, controller, library: "books", state: "bookmarked")
            context.overlay.reset()
            control(canvas, "settings").activate()
            try capture(canvas, context, controller, library: "books", state: "settings")
            canvas.performAccessibilityAction(item(canvas, label: "Left to right"))
            spin { !controller.preferences.vertical && controller.current != nil && !controller.loading }
            settle()
            try capture(canvas, context, controller, library: "books", state: "settingsPaged")
            canvas.performAccessibilityAction("fontSize:plus")
            spin { controller.preferences.fontSize == 11 && controller.current != nil && !controller.loading }
            settle()
            try capture(canvas, context, controller, library: "books", state: "settingsFontSize")
            _ = canvas.keyDown(key(53))
            try capture(canvas, context, controller, library: "books", state: "paged")
            let before = controller.current!.location
            control(canvas, "next").activate()
            spin { controller.current != nil && controller.current!.location != before && !controller.loading }
            settle()
            try capture(canvas, context, controller, library: "books", state: "pagedNext")
            canvas.page.tabs!.tabs[3].activate()
            try capture(canvas, context, controller, library: "books", state: "marks")
            canvas.performAccessibilityAction("mark:" + bookmarkIDs[0] + ":remove")
            spin { controller.book?.bookmarks.isEmpty == true }
            settle()
            try capture(canvas, context, controller, library: "books", state: "marksEmpty")
            canvas.page.tabs!.tabs[1].activate()
            try capture(canvas, context, controller, library: "books", state: "librarySelected")
            tile(2).activate()
            spin { controller.current != nil && controller.book?.id == bookID(2) && canvas.page.controls.contains { $0.id == "bookmark" } }
            settle()
            try capture(canvas, context, controller, library: "books", state: "readingPdf")
            L10n.language = .simplifiedChinese; canvas.setActive(false); show(); canvas.setActive(true)
            spin { controller.current != nil && canvas.page.controls.contains { $0.id == "bookmark" } }
            settle()
            try capture(canvas, context, controller, library: "books", state: "readingPdf", language: .simplifiedChinese)
            canvas.page.tabs!.tabs[1].activate()
            // The reading flag's write lands before the next tab change: ReaderController.setReading compares with the
            // last CONFIRMED snapshot, so a switch back while the write is in flight is dropped (a Mac race; Windows
            // compares with the pending intent).
            settle()
            try capture(canvas, context, controller, library: "books", state: "librarySelected", language: .simplifiedChinese)
            L10n.language = .english; canvas.setActive(false); show(); canvas.setActive(true)
            spin { canvas.missing == [bookID(3)] }
            themed(true); context.refreshPalette(); show()
            try capture(canvas, context, controller, library: "books", state: "librarySelected", light: true)
            canvas.page.tabs!.tabs[2].activate()
            spin { controller.current != nil && canvas.page.controls.contains { $0.id == "bookmark" } }
            settle()
            try capture(canvas, context, controller, library: "books", state: "readingPdf", light: true)
            themed(false); context.refreshPalette(); show()
            canvas.setActive(false)
            spin { controller.isReading == (canvas.tab == .reading) }
            payload["readerFinal"] = ["reading": controller.isReading, "selected": controller.book?.id.uuidString as Any? ?? NSNull()]
        }
        payload["reader"] = ["books": bookRows, "missing": [bookID(3).uuidString], "bookmarkIDs": bookmarkIDs,
                             "pages": Array(rendered.values), "shelf": shelf.map { ["id": $0.id.uuidString, "title": $0.title,
                                 "supported": $0.isSupported, "available": $0.isAvailable] }]

        // MARK: ReaderStore steps (library.json bytes, the optional `reading` key)
        do {
            let directory = scratch.appendingPathComponent("Store", isDirectory: true)
            try writeLibrary(directory, books: Array(archiveBooks.prefix(2)))
            let store = try ReaderStore(directory: directory)
            var steps: [[String: Any]] = []
            func step(_ name: String) throws {
                steps.append(["step": name, "json": try String(contentsOf: directory.appendingPathComponent("library.json"), encoding: .utf8)])
            }
            try step("initial")
            try store.select(bookID(2)); try step("select")
            try store.setReading(true); try step("reading on")
            try store.saveProgress(ReaderLocation(section: 1, block: 0, character: 0), progress: 0.5, id: bookID(2)); try step("progress")
            try store.toggleBookmark(id: bookID(2)); try step("bookmark")
            try store.setReading(false); try step("reading off")
            try store.setReading(true); try store.remove(bookID(2)); try step("remove selected")
            payload["store"] = steps
        }

        // MARK: Projection
        do {
            let suite = "ehud.projection.oracle.\(UUID().uuidString)"
            let defaults = UserDefaults(suiteName: suite)!
            defer { defaults.removePersistentDomain(forName: suite) }
            let settings = ProjectionSettings(defaults: defaults)
            let model = ProjectionModel(configuration: configuration)
            let context = HUDDesignContext(env: env)
            var opened = 0
            let canvas = ProjectionCanvas(context: context, settings: settings)
            canvas.onOpen = { opened += 1 }
            func show() { _ = canvas.makeContent(for: .projection, style: style) }
            var projectionRows: [[String: Any]] = []
            func stored() -> Any { defaults.dictionary(forKey: "projection.settings.v1").map { sanitize($0) } ?? NSNull() }
            func capture(_ state: String, language: AppLanguage = .english, light: Bool = false) throws {
                var row: [String: Any] = ["module": "projection", "state": state, "language": languageName(language), "light": light,
                    "layer": try recorder.tree(canvas.root, id: "root"),
                    "controls": (canvas.header.controls + canvas.page.controls).map { controlState($0, in: canvas.root) },
                    "accessibility": accessibility(canvas.accessibilityItems),
                    "header": ["counter": canvas.header.counterTextForTesting, "title": canvas.header.titleTextForTesting.0],
                    "values": ["color": settings.values.color, "width": settings.values.width, "erasing": settings.values.erasing,
                               "background": settings.values.background],
                    "stored": stored(), "opened": opened, "mediaInsert": settings.mediaInsertRequested,
                    "strokes": model.drawing.strokes.count]
                if canvas.isMenuOpen || canvas.isPopoverOpen { row["top"] = try recorder.tree(canvas.top, id: "top") }
                row["toast"] = context.overlay.toastState.current.map { ["title": $0.title, "caption": $0.caption] } ?? NSNull()
                row["confirm"] = context.overlay.confirmState.request.map { ["id": $0.id, "title": $0.title, "message": $0.message,
                                                                              "confirm": $0.confirmTitle, "danger": $0.danger] } ?? NSNull()
                projectionRows.append(row)
            }
            show(); canvas.setActive(true)
            try capture("detached")
            canvas.setActive(false)
            settings.attach(model)
            model.append(NotesDrawingStroke(points: [NotesDrawingPoint(x: 0.1, y: 0.1), NotesDrawingPoint(x: 0.8, y: 0.4)], width: 12,
                                            color: NotesRGBA(red: 1, green: 0.83, blue: 0.12, alpha: 1)))
            show(); canvas.setActive(true)
            try capture("default")
            control(canvas, "eraser").activate()
            try capture("eraser")
            control(canvas, "dotted").activate()
            try capture("backgroundOff")
            control(canvas, "color:22A6F2").activate()
            try capture("cyan")
            canvas.adjustAccessibilitySlider("penWidth", value: 30)
            try capture("penWidth")
            canvas.setActive(false)
            model.color = NSColor(srgbRed: 0x12 / 255.0, green: 0x34 / 255.0, blue: 0x56 / 255.0, alpha: 1)
            show(); canvas.setActive(true)
            try capture("custom")
            control(canvas, "color:custom").activate()
            try capture("colorPicker")
            _ = canvas.keyDown(key(53))
            control(canvas, "clear").activate()
            try capture("clearConfirm")
            context.overlay.decide(.confirm)
            try capture("cleared")
            context.overlay.reset()
            control(canvas, "media").activate()
            try capture("media")
            _ = settings.takeMediaInsertRequest()
            control(canvas, "open").activate()
            try capture("open")
            context.overlay.reset()
            L10n.language = .simplifiedChinese; canvas.setActive(false); show(); canvas.setActive(true)
            try capture("custom", language: .simplifiedChinese)
            L10n.language = .english; canvas.setActive(false); show(); canvas.setActive(true)
            themed(true); context.refreshPalette(); show()
            try capture("custom", light: true)
            themed(false); context.refreshPalette(); show()
            canvas.setActive(false)
            payload["projection"] = projectionRows
        }

        payload["canvas"] = rows
        payload["text"] = ["widths": Array(recorder.widths.values), "metrics": Array(recorder.metrics.values)]
        payload["layerDefaults"] = recorder.layerDefaults
        let data = try JSONSerialization.data(withJSONObject: sanitize(payload), options: [.sortedKeys])
        try data.write(to: output.appendingPathComponent("reader-projection-source.json"))
        print("Exported \(rows.count) Reader scenes, \((payload["projection"] as? [Any])?.count ?? 0) Projection scenes")
    }
}
