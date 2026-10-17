import AppKit
import QuartzCore

/// Runs the unchanged NotesCanvas with disposable records and detached layers.
/// This exports model artwork and animation descriptors, not presentation pixels.
@main enum NotesDeletionReference {
    static func require(_ value: @autoclosure () -> Bool, _ message: String) throws {
        if !value() { throw HUDSourceError.invalid(message) }
    }
    static func run() throws {
        let arguments = CommandLine.arguments
        try require(arguments.count == 4 && arguments[1] == "--ui-test" && arguments[2] == "--output", "Use notes_deletion_reference.sh")
        try require(ProcessInfo.processInfo.environment["CFFIXED_USER_HOME"] != nil, "Isolated fixture home required")
        let output = URL(fileURLWithPath: arguments[3], isDirectory: true)
        let temporary = FileManager.default.temporaryDirectory.appendingPathComponent("notes-deletion-" + UUID().uuidString, isDirectory: true)
        try FileManager.default.createDirectory(at: temporary, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: temporary) }
        NSApplication.shared.setActivationPolicy(.prohibited)
        var configuration = AppConfiguration.defaults
        configuration.language = .english; configuration.ambientAnimation = false
        configuration.reduceMotion = false; configuration.launchAtLogin = false
        HUDRuntimeAppearance.configuration = configuration; L10n.language = .english
        let encoder = try ModuleReferenceLayerEncoder(output: output)
        // The normal host supplies the same configured color as the global
        // highlight layer. Preserve its exact 8-bit-to-Double conversion.
        let accent = configuration.accentColor
        let uuid = UUID(uuidString: "00000000-0000-4000-8000-000000000001")!
        let date = Date(timeIntervalSince1970: 1_700_000_000)
        let scenarios: [(String, CGRect, CGPoint)] = [
            ("interior", CGRect(x: 0, y: 0, width: 1280, height: 800), CGPoint(x: 60, y: 90)),
            ("offset-bottom-right", CGRect(x: 17, y: 23, width: 360, height: 160), CGPoint(x: 350, y: 155))
        ]
        var cases: [[String: Any]] = []
        for dark in [true, false] { for reduced in [false, true] { for (name, bounds, origin) in scenarios {
            let key = "\(name)-\(dark ? "dark" : "light")-\(reduced ? "reduced" : "animated")"
            let store = try NotesStore(directory: temporary.appendingPathComponent(key))
            try store.upsert(CanvasNote(id: uuid, kind: .text, text: "Synthetic confirmation", x: origin.x, y: origin.y,
                width: 210, height: 140, zIndex: 1, createdAt: date))
            let canvas = NotesCanvas(store: store, notesSelected: true, reduceMotion: { reduced })
            defer { canvas.setVisible(false); canvas.cancelInteraction(); canvas.cancelAnimations() }
            canvas.setWorkspaceBounds(bounds)
            canvas.updateAppearance(style: HUDModuleContentStyle(dark: dark, accent: accent, contentsScale: 2))
            let prefix = "note:" + uuid.uuidString + ":"
            canvas.perform(actionID: prefix + "delete")
            try require(canvas.pendingDeletionID == uuid && store.notes.count == 1, "Request must not delete or persist a record")
            guard let layer = canvas.workspaceLayer.sublayers?.first(where: { $0.name == "notes.deleteConfirmation" }) else {
                throw HUDSourceError.invalid("Original confirmation layer missing")
            }
            let actions = canvas.accessibleActions.filter { $0.id == prefix + "cancelDelete" || $0.id == prefix + "confirmDelete" }
            try require(actions.count == 2 && layer.sublayers?.count == 2, "Original confirmation must have exactly two controls")
            let displayed = canvas.accessibleActions.first(where: { $0.id == prefix + "select" })!.rect
            var animation: Any = NSNull()
            if let rise = layer.animation(forKey: "notes.confirm.reveal") as? CABasicAnimation {
                var controlPoints: [[Float]] = []
                if let timing = rise.timingFunction {
                    for index in 0..<4 { var values: [Float] = [0, 0]; timing.getControlPoint(at: index, values: &values); controlPoints.append(values) }
                }
                animation = ["keyPath": rise.keyPath as Any, "from": rise.fromValue as Any, "to": rise.toValue as Any,
                    "duration": rise.duration, "timingControlPoints": controlPoints]
            }
            try require(reduced == (animation is NSNull), "Only animated fixture should have an attached reveal animation")
            cases.append(["name": key, "dark": dark, "reduceMotion": reduced,
                "accent": [accent.redComponent, accent.greenComponent, accent.blueComponent, accent.alphaComponent],
                "workspaceBounds": ModuleReferenceLayerEncoder.rect(bounds), "displayedCard": ModuleReferenceLayerEncoder.rect(displayed),
                "actions": actions.map { ["id": String($0.id.dropFirst(prefix.count)), "label": $0.label, "rect": ModuleReferenceLayerEncoder.rect($0.rect)] },
                "root": try encoder.encode(layer, id: key), "animation": animation])
            canvas.perform(actionID: prefix + "cancelDelete")
            try require(canvas.pendingDeletionID == nil && layer.isHidden && store.notes.count == 1, "Cancel must hide confirmation without removing record")
        } } }
        try require(NSApp.windows.isEmpty && encoder.rasterAssets.isEmpty, "Fixture must remain windowless and vector-only")
        let value: [String: Any] = ["schemaVersion": 1, "scope": "unchanged original NotesCanvas deletion confirmation model layers and attached animation descriptor",
            "cases": cases, "rasterAssets": encoder.rasterAssets, "unsupported": encoder.unsupported,
            "isolation": ["windowCreated": false, "screenCapture": false, "temporaryStoresOnly": true, "nativeCursorCalls": false],
            "notVerified": ["Core Animation presentation pixels", "Windows antialiasing", "native input dispatch"]]
        try JSONSerialization.data(withJSONObject: value, options: [.sortedKeys, .withoutEscapingSlashes])
            .write(to: output.appendingPathComponent("confirmation.json"), options: .withoutOverwriting)
        print("Exported \(cases.count) original Notes confirmation states; no window or capture")
    }
    static func main() { do { try run() } catch { fputs("Notes deletion reference: \(error)\n", stderr); exit(1) } }
}
