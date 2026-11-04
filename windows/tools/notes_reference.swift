import AppKit
import QuartzCore

// Actual-source oracle of the redesigned Notes centre (Mac 1.3.0 Sources/NotesCanvas.swift with its real
// NotesCentreStore and NotesFormatBar over synthetic documents). The unchanged Mac canvas is built in scripted
// states (an empty TODO list, three tasks with a checked one and an orange dot, the task menu, Hide, the TEXT note
// with its format bar, the storage notice, Recording mode; English, Simplified Chinese, Light) and exported as its
// model layer tree with the design recorder (tools/hud_design_reference_support.swift), its page controls and
// accessibility items, and the scenario's INPUT (the centre.json bytes the canvas read), so the Windows suite
// rebuilds the same state. Also exported: the format bar's statics (sizes, swatches, the demo families).
// Isolation: temporary CFFIXED_USER_HOME (the stores live there), no window, no input posted, Reduce Motion on so
// every page installs at once, TZ=UTC.

private var reduce = true
private let env = HUDDesignEnvironment(renderScale: 2, reduceMotion: { reduce })

@main private enum NotesReference {
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
        var payload: [String: Any] = ["schema": 1]
        let style = HUDModuleContentStyle(dark: true, accent: .yellow, contentsScale: 2)
        let context = HUDDesignContext(env: env)
        var serial = 0
        let ids = (1...4).map { UUID(uuidString: String(format: "0000000C-0000-4000-8000-%012d", $0))! }
        let three: (inout NotesCentreDocument) -> Void = { d in
            d.tasks = [NoteChecklistItem(id: ids[0], text: "Calibrate the relay", isChecked: true),
                       NoteChecklistItem(id: ids[1], text: "Patrol route 7", isChecked: false),
                       NoteChecklistItem(id: ids[2], text: "Report to Perlica", isChecked: false)]
            d.flagged = [ids[1]]
        }
        // One canvas per scene over its own stores (the document is what the Windows suite loads).
        func canvas(storage: Bool = true, _ change: (inout NotesCentreDocument) -> Void) throws -> (NotesCanvas, Data) {
            serial += 1
            let directory = URL(fileURLWithPath: home).appendingPathComponent("Notes-\(serial)", isDirectory: true)
            try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
            let centre = NotesCentreStore(directory: storage ? directory : nil)
            try centre.update(change)
            let store = storage ? try NotesStore(directory: directory) : nil
            let canvas = NotesCanvas(context: context, store: store, centre: centre)
            let encoder = JSONEncoder(); encoder.outputFormatting = [.sortedKeys]
            return (canvas, try encoder.encode(centre.document))
        }
        func show(_ c: NotesCanvas) { _ = c.makeContent(for: .notes, style: style); c.setActive(true) }
        func row(_ state: String, _ c: NotesCanvas, _ document: Data, language: AppLanguage = .english, light: Bool = false, storage: Bool = true) throws -> [String: Any] {
            var row: [String: Any] = ["state": state, "language": languageName(language), "light": light, "storage": storage,
                "layer": try recorder.tree(c.root, id: "root"),
                "controls": c.page.controls.map { controlState($0, in: c.root) },
                "accessibility": accessibility(c.accessibilityItems),
                "input": ["document": String(data: document, encoding: .utf8)!, "recording": context.recordingMode],
                "header": ["counter": c.header.counterTextForTesting, "title": c.header.titleTextForTesting.0]]
            if c.isMenuOpen { row["top"] = try recorder.tree(c.top, id: "top") }
            return row
        }
        var rows: [[String: Any]] = []
        func scene(_ state: String, language: AppLanguage = .english, light: Bool = false, storage: Bool = true,
                   _ change: (inout NotesCentreDocument) -> Void, step: (NotesCanvas) -> Void = { _ in }) throws {
            L10n.language = language
            HUDColor.apply(accentHex: "FAD41F", light: light); context.refreshPalette()
            let (c, document) = try canvas(storage: storage, change)
            show(c); step(c)
            rows.append(try row(state, c, document, language: language, light: light, storage: storage))
            c.setActive(false)
        }
        try scene("todoEmpty") { _ in }
        try scene("todo", three)
        try scene("todo", language: .simplifiedChinese, three)
        try scene("todo", light: true, three)
        try scene("taskMenu", three) { $0.openTaskMenuForTesting(ids[1]) }
        try scene("folded") { d in three(&d); d.isFolded = true }
        try scene("text") { d in d.tab = .text; d.text = "Field report\nSecond line" }
        try scene("textEmpty") { d in d.tab = .text }
        try scene("textStyled") { d in
            d.tab = .text; d.text = "Bold heading"
            var s = NotesTextStyle(); s.bold = true; s.fontSize = 18; s.color = NotesRGBA(red: 0x22 / 255.0, green: 0xA6 / 255.0, blue: 0xF2 / 255.0)
            d.richText = NotesRichText(version: 1, runs: [NotesTextRun(location: 0, length: 12, style: s)])
        }
        try scene("unavailable", storage: false) { _ in }
        context.setRecordingMode(true)
        try scene("recording", three)
        context.setRecordingMode(false)
        L10n.language = .english
        payload["notes"] = rows
        // The desktop cards' actions (NotesWorkspace.accessibleActions): the hit geometry of each card in workspace
        // points, settled, selected, editing and with a pending deletion.
        let desk = URL(fileURLWithPath: home).appendingPathComponent("Desk", isDirectory: true)
        try FileManager.default.createDirectory(at: desk, withIntermediateDirectories: true)
        let deskStore = try NotesStore(directory: desk)
        let text = UUID(uuidString: "0000000D-0000-4000-8000-000000000001")!, todo = UUID(uuidString: "0000000D-0000-4000-8000-000000000002")!
        let drawing = UUID(uuidString: "0000000D-0000-4000-8000-000000000003")!
        let itemA = UUID(uuidString: "0000000D-0000-4000-8000-0000000000A1")!, itemB = UUID(uuidString: "0000000D-0000-4000-8000-0000000000A2")!
        let date = Date(timeIntervalSince1970: 1_700_000_000)
        try deskStore.upsert(CanvasNote(id: text, kind: .text, text: "One\nTwo", x: 60, y: 90, width: 260, height: 140, zIndex: 1, createdAt: date))
        try deskStore.upsert(CanvasNote(id: todo, kind: .todo, items: [NoteChecklistItem(id: itemA, text: "First", isChecked: true), NoteChecklistItem(id: itemB, text: "Second")],
            x: 360, y: 90, width: 228, height: 154, zIndex: 2, createdAt: date))
        try deskStore.upsert(CanvasNote(id: drawing, kind: .drawing, x: 60, y: 300, width: 300, height: 240, zIndex: 3, createdAt: date, drawing: NotesDrawing()))
        let deskCanvas = NotesCanvas(context: context, store: deskStore, centre: NotesCentreStore(directory: nil))
        deskCanvas.setWorkspaceBounds(CGRect(x: 0, y: 0, width: 1920, height: 1080))
        show(deskCanvas)
        var desktop: [[String: Any]] = []
        func actions(_ state: String) {
            desktop.append(["state": state, "actions": deskCanvas.accessibleActions.map { ["id": $0.id, "label": $0.label, "rect": rect($0.rect)] }])
        }
        actions("settled")
        deskCanvas.perform(actionID: "note:\(drawing.uuidString):select"); actions("drawingSelected")
        deskCanvas.perform(actionID: "note:\(text.uuidString):edit"); actions("textEditing")
        deskCanvas.perform(actionID: "note:\(text.uuidString):delete"); actions("deletionPending")
        payload["desktop"] = desktop
        deskCanvas.setActive(false)
        payload["formatBar"] = ["sizes": NotesFormatBar.sizes, "families": NotesFormatBar.families, "colors": NotesFormatBar.colors.map { Int($0) }]
        payload["text"] = ["widths": Array(recorder.widths.values), "metrics": Array(recorder.metrics.values)]
        payload["layerDefaults"] = recorder.layerDefaults
        let data = try JSONSerialization.data(withJSONObject: sanitize(payload), options: [.sortedKeys])
        try data.write(to: output.appendingPathComponent("notes-canvas-source.json"))
        print("Exported \(rows.count) Notes centre scenes")
    }
}
