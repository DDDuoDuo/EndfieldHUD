import AppKit
import QuartzCore

// Actual-source oracle of the redesigned Archive (Mac 1.3.0 Sources/ArchiveCanvas.swift over its real ArchiveStore and
// ArchiveController with ArchiveMemoryPasswordVault and synchronous executors). The unchanged Mac canvas is built in
// scripted states (the empty list; three categories with a NEW badge and four documents, one classified; a category
// filter; the category rename; the editor of a written document, of a new document, its category menu and its Classify
// pop-over; the locked page; the recovery page of a classified document whose password is gone; Recording mode;
// English, Simplified Chinese, Light) and exported as its model layer tree with the design recorder
// (tools/hud_design_reference_support.swift), its page controls and accessibility items, and the scenario's INPUT (the
// store's categories with their NEW flags, its entries as the store's JSON payloads, the classified ids, the selection,
// the filter), so the Windows suite rebuilds the same state. Also exported: the font menu's families and the widths of
// the locked page's redaction lines. The body text view is a native view outside the layer tree (no host: never
// attached), so the tree holds the body area's plate and placeholder only.
// Isolation: temporary CFFIXED_USER_HOME (every store lives there), no window, no input posted, Reduce Motion on so every
// page installs at once, TZ=UTC.

private var reduce = true
private let env = HUDDesignEnvironment(renderScale: 2, reduceMotion: { reduce })

private func pump() { RunLoop.main.run(until: Date().addingTimeInterval(0.15)) }

@main private enum ArchiveCanvasReference {
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
        let password = "correct horse"

        func uuid(_ n: Int) -> UUID { UUID(uuidString: String(format: "0000000A-0000-4000-8000-%012d", n))! }
        func day(_ d: Int, _ hour: Int = 9) -> Date { Date(timeIntervalSinceReferenceDate: 814_492_800 + Double(d) * 86_400 + Double(hour) * 3600) }
        let operations = ArchiveCategory(id: uuid(1), name: "Operations", created: day(0), isNew: false)
        let research = ArchiveCategory(id: uuid(2), name: "Research", created: day(1), isNew: false)
        let signals = ArchiveCategory(id: uuid(3), name: "Signals", created: day(2), isNew: true)
        func entry(_ n: Int, _ title: String, _ body: String, _ category: UUID?, _ date: Date) -> ArchiveEntry {
            var e = ArchiveEntry(template: .journal)
            e.id = uuid(10 + n); e.title = title; e.body = body; e.categoryID = category; e.date = date; e.modified = date
            return e
        }
        let report = entry(1, "Field report", "Relay calibrated at 06:00.\nNo anomalies on route 7; the beacon at the ridge answers every sweep.", operations.id, day(3))
        let patrol = entry(2, "Patrol log", "", nil, day(4))
        let sealed = entry(3, "Sealed brief", "Perlica's orders for the next rotation. Keep this away from the field team until the briefing is over.\nSecond page.", research.id, day(5))
        let untitled = entry(4, "", "Loose note", nil, day(6))
        var serial = 0

        struct Scene { let store: ArchiveStore; let vault: ArchiveMemoryPasswordVault; let controller: ArchiveController; let canvas: ArchiveCanvas
            let categories: [ArchiveCategory]; let entries: [ArchiveEntry]; let classified: [UUID] }
        func make(categories: [ArchiveCategory], entries: [ArchiveEntry], classified: [UUID] = [], forget: Bool = false) throws -> Scene {
            serial += 1
            let directory = URL(fileURLWithPath: home).appendingPathComponent("Archive-\(serial)", isDirectory: true)
            try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
            let vault = ArchiveMemoryPasswordVault()
            let store = ArchiveStore(directory: directory, vault: vault)
            for c in categories { try store.saveCategory(c) }
            for e in entries { try store.save(e) }
            for id in classified { try store.classify(id, password: password) }
            if forget { for account in try vault.accounts() { try vault.remove(account: account) } }
            let controller = ArchiveController(store: store, work: { $0() }, deliver: { $0() }, ownerAuthentication: nil)
            let canvas = ArchiveCanvas(context: context, controller: controller)
            return Scene(store: store, vault: vault, controller: controller, canvas: canvas, categories: categories, entries: entries, classified: classified)
        }
        func control(_ c: ArchiveCanvas, _ id: String) -> HUDControl? { (c.page.controls + c.header.controls).first { $0.id == id } }
        func activate(_ c: ArchiveCanvas, _ id: String) {
            guard let control = control(c, id) else { fatalError("No control \(id)") }
            control.activate(); pump()
        }
        func input(_ s: Scene) throws -> [String: Any] {
            let encoder = JSONEncoder(); encoder.outputFormatting = [.sortedKeys]
            let filter: String
            switch s.canvas.filter {
            case .all: filter = "all"
            case .uncategorized: filter = "uncategorized"
            case .category(let id): filter = id.uuidString
            }
            // The store's categories after the canvas ran (NEW flags as stored).
            let categories = try s.store.categories().map { c -> [String: Any] in
                ["id": c.id.uuidString, "name": c.name, "created": c.created.timeIntervalSinceReferenceDate, "isNew": c.isNew]
            }
            return ["categories": categories,
                    "entries": try s.entries.map { String(data: try encoder.encode($0), encoding: .utf8)! },
                    "classified": s.classified.map(\.uuidString), "passwordsForgotten": try s.vault.accounts().isEmpty && !s.classified.isEmpty,
                    "selected": s.controller.selected?.id.uuidString as Any? ?? NSNull(), "filter": filter,
                    // The open document as the controller holds it (a new one's id and date come from here).
                    "selectedEntry": try s.controller.selected.map { String(data: try encoder.encode($0), encoding: .utf8)! } as Any? ?? NSNull(),
                    "page": { switch s.canvas.displayedPage { case .list: return "list"; case .editor: return "editor"; case .locked: return "locked" } }(),
                    "recording": context.recordingMode]
        }
        func row(_ state: String, _ s: Scene, language: AppLanguage, light: Bool) throws -> [String: Any] {
            let c = s.canvas
            var row: [String: Any] = ["state": state, "language": languageName(language), "light": light,
                "layer": try recorder.tree(c.root, id: "root"),
                "controls": c.page.controls.map { controlState($0, in: c.root) },
                "accessibility": accessibility(c.accessibilityItems), "input": try input(s),
                "header": ["counter": c.header.counterTextForTesting, "title": c.header.titleTextForTesting.0,
                           "crumbs": c.header.crumbTextsForTesting]]
            if c.isMenuOpen || c.isPopoverOpen { row["top"] = try recorder.tree(c.top, id: "top") }
            return row
        }
        var rows: [[String: Any]] = []
        let all = [operations, research, signals], documents = [report, patrol, sealed, untitled]
        func scene(_ state: String, language: AppLanguage = .english, light: Bool = false, recording: Bool = false,
                   categories: [ArchiveCategory]? = nil, entries: [ArchiveEntry]? = nil, classified: [UUID] = [sealed.id], forget: Bool = false,
                   _ step: (ArchiveCanvas) -> Void = { _ in }) throws {
            L10n.language = language
            HUDColor.apply(accentHex: "FAD41F", light: light); context.refreshPalette()
            context.setRecordingMode(recording)
            let s = try make(categories: categories ?? all, entries: entries ?? documents, classified: classified, forget: forget)
            _ = s.canvas.makeContent(for: .archive, style: style); s.canvas.setActive(true); pump()
            step(s.canvas); pump()
            rows.append(try row(state, s, language: language, light: light))
            s.canvas.setActive(false)
            context.setRecordingMode(false)
        }
        try scene("listEmpty", categories: [], entries: [], classified: [])
        try scene("listEmpty", language: .simplifiedChinese, categories: [], entries: [], classified: [])
        try scene("list")
        try scene("list", language: .simplifiedChinese)
        try scene("list", light: true)
        try scene("recording", recording: true)
        try scene("filterCategory") { activate($0, "category:" + operations.id.uuidString) }
        try scene("filterNew") { activate($0, "category:" + signals.id.uuidString) }
        try scene("filterUncategorized") { activate($0, "category:uncategorized") }
        try scene("rename") { c in activate(c, "category:" + research.id.uuidString); c.beginRename(); pump() }
        try scene("editor") { activate($0, "doc:" + report.id.uuidString) }
        try scene("editor", language: .simplifiedChinese) { activate($0, "doc:" + report.id.uuidString) }
        try scene("editor", light: true) { activate($0, "doc:" + report.id.uuidString) }
        try scene("editorUntitled") { activate($0, "doc:" + untitled.id.uuidString) }
        try scene("editorNew") { activate($0, "new") }
        try scene("editorNewInCategory") { c in activate(c, "category:" + research.id.uuidString); activate(c, "new") }
        try scene("categoryMenu") { c in activate(c, "doc:" + report.id.uuidString); activate(c, "category") }
        try scene("classifyPopover") { c in activate(c, "doc:" + patrol.id.uuidString); activate(c, "classify") }
        try scene("locked") { activate($0, "doc:" + sealed.id.uuidString) }
        try scene("locked", language: .simplifiedChinese) { activate($0, "doc:" + sealed.id.uuidString) }
        try scene("passwordMissing", forget: true) { activate($0, "doc:" + sealed.id.uuidString) }
        try scene("recoverPopover", forget: true) { c in activate(c, "doc:" + sealed.id.uuidString); activate(c, "recover") }
        L10n.language = .english
        HUDColor.apply(accentHex: "FAD41F", light: false); context.refreshPalette()
        payload["archive"] = rows
        // The locked page's redaction bars measure each line (ArchiveLockedPanel.init).
        for language in [AppLanguage.english, .simplifiedChinese] {
            L10n.language = language
            let tag = HUDTextLanguage.tag()
            for title in [sealed.title, "Untitled", "未命名"] { recorder.width(title, ArchiveLockedPanel.titleStyle, language: tag) }
            for line in ArchiveLockedPanel.lines(sealed.body) { recorder.width(line, ArchiveLockedPanel.lineStyle, language: tag) }
        }
        L10n.language = .english
        payload["lockedLines"] = ArchiveLockedPanel.lines(sealed.body)
        payload["families"] = ArchiveRichText.families
        payload["text"] = ["widths": Array(recorder.widths.values), "metrics": Array(recorder.metrics.values)]
        payload["layerDefaults"] = recorder.layerDefaults
        let data = try JSONSerialization.data(withJSONObject: sanitize(payload), options: [.sortedKeys])
        try data.write(to: output.appendingPathComponent("archive-canvas-source.json"))
        print("Exported \(rows.count) Archive scenes")
    }
}
