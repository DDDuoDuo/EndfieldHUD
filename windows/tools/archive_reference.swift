import AppKit
import QuartzCore

/// Actual detached ArchiveCanvas/private source menus. The only database is
/// inside CFFIXED_USER_HOME; original media decoders/editors are not activated.
@main enum ArchiveReference {
    static func require(_ value: @autoclosure () -> Bool, _ message: String) throws {
        if !value() { throw HUDSourceError.invalid(message) }
    }
    static func write(_ value: Any, _ url: URL) throws {
        try JSONSerialization.data(withJSONObject: value, options: [.prettyPrinted, .sortedKeys, .withoutEscapingSlashes]).write(to: url, options: .atomic)
    }
    static func encoded<T: Encodable>(_ value: T) throws -> Any {
        try JSONSerialization.jsonObject(with: JSONEncoder().encode(value))
    }
    static func run() throws {
        try require(Thread.isMainThread && CommandLine.arguments.count == 3 && CommandLine.arguments[1] == "--output", "Use isolated archive_reference.sh")
        guard let home = ProcessInfo.processInfo.environment["CFFIXED_USER_HOME"] else { throw HUDSourceError.invalid("Temporary home is required") }
        let output = URL(fileURLWithPath: CommandLine.arguments[2], isDirectory: true)
        NSApplication.shared.setActivationPolicy(.prohibited)
        var configuration = AppConfiguration.defaults
        configuration.language = .english; configuration.theme = .dark; configuration.ambientAnimation = false
        configuration.reduceMotion = false; configuration.launchAtLogin = false
        HUDRuntimeAppearance.configuration = configuration; L10n.language = .english
        let store = ArchiveStore(directory: URL(fileURLWithPath: home, isDirectory: true).appendingPathComponent("Archive"))
        let controller = ArchiveController(store: store, work: { $0() }, deliver: { $0() })
        let canvas = ArchiveCanvas(controller: controller)
        defer { controller.deactivate() }
        let encoder = try ModuleReferenceLayerEncoder(output: output)
        _ = canvas.makeContent(for: .archive, style: .init(dark: true, accent: configuration.accentColor, contentsScale: 2))
        controller.activate(); canvas.render()
        var rows: [[String: Any]] = []
        func actions(_ values: [NotesRetainedMenu.Item]) -> [[String: Any]] {
            values.map { ["id": $0.id, "title": $0.title, "rect": ModuleReferenceLayerEncoder.rect($0.rect), "enabled": $0.enabled, "selected": $0.selected] }
        }
        func emit(_ name: String, kind: String = "canvas", menu: NotesRetainedMenu? = nil) throws {
            var row: [String: Any] = ["schemaVersion": 1, "state": name, "kind": kind,
                "root": try encoder.encode(menu?.artwork ?? canvas.layer, id: "archive/" + name),
                "actions": actions(menu?.items ?? canvas.actions), "dark": canvas.dark,
                "entries": try encoded(controller.entries.map { summary in
                    // This is source store metadata, not a reconstructed model.
                    try store.entry(summary.id)!
                }), "categories": try encoded(controller.categories),
                "selectedID": controller.selected?.id.uuidString as Any? ?? NSNull(),
                "categoryID": canvas.categoryID?.uuidString as Any? ?? NSNull(),
                "filtersUncategorized": canvas.filtersUncategorized,
                "scrollOffset": canvas.scrollOffset, "categoryScrollOffset": canvas.categoryScrollOffset,
                "galleryScrollMaximum": canvas.galleryScrollMaximum,
                "galleryScrollThumb": canvas.galleryScrollThumb.map(ModuleReferenceLayerEncoder.rect) as Any? ?? NSNull(),
                "bodyRect": ModuleReferenceLayerEncoder.rect(canvas.bodyRect), "mediaIndex": canvas.mediaIndex,
                "formattingField": canvas.formattingField as Any? ?? NSNull()]
            if let menu {
                row["menuSize"] = [menu.contentSize.width, menu.contentSize.height]
                row["menuScrollOffset"] = endfieldArchiveReferenceScrollOffset(menu)
            }
            if let face = canvas.layer.sublayers?.first(where: { $0.name == "archive.face" }), let animation = face.animation(forKey: "archive.transition") as? CABasicAnimation {
                row["faceTransition"] = ["keyPath": animation.keyPath ?? "", "from": animation.fromValue as Any? ?? NSNull(), "to": animation.toValue as Any? ?? NSNull(), "duration": animation.duration, "retireAfter": 0.2]
            }
            let file = "archive-" + name + ".json"; try write(row, output.appendingPathComponent(file)); rows.append(["file": file, "state": name, "kind": kind])
        }
        try emit("empty")
        let timestamp = Date(timeIntervalSince1970: 1_790_000_000)
        for index in 0..<14 {
            let id = UUID(uuidString: String(format: "A0000000-0000-4000-8000-%012d", index + 1))!
            try store.saveCategory(ArchiveCategory(id: id, name: index == 2 ? "分类中文 / 긴 이름" : "Category \(index)", created: timestamp.addingTimeInterval(Double(index))))
        }
        let categories = try store.categories()
        for index in 0..<20 {
            let id = UUID(uuidString: String(format: "B0000000-0000-4000-8000-%012d", index + 1))!
            var entry = ArchiveEntry(id: id, template: index % 2 == 0 ? .journal : .research, title: index == 0 ? "" : "Document \(index)", date: timestamp, body: index == 1 ? "" : "A source document.\n中文、한국어 and emoji 🐱", media: [], modified: timestamp.addingTimeInterval(Double(index)), categoryID: index % 3 == 0 ? nil : categories[index % categories.count].id)
            if index == 2 {
                entry.titleStyle = NotesTextStyle(fontSize: 17, bold: true)
                entry.bodyRichText = NotesRichText(runs: [.init(location: 2, length: 6, style: .init(fontSize: 24, color: .init(red: 0.3, green: 0.7, blue: 0.4, alpha: 1), italic: true))])
            }
            if index == 3 || index == 4 {
                let kind: NotesMediaKind = index == 3 ? .image : .video
                entry.media = [NotesMediaReference(kind: kind, bookmark: Data([1]), isSecurityScoped: false,
                    lastKnownPath: "/__endfield_fixture_not_opened__/asset", displayName: "Synthetic reference", pixelWidth: 1920, pixelHeight: 1080, duration: kind == .video ? 90 : nil, frameCount: 1)]
            }
            try store.save(entry)
        }
        controller.activate(); canvas.render(); try emit("gallery")
        canvas.scroll(at: CGPoint(x: 150, y: 200), delta: 44.5); try emit("gallery-scroll")
        canvas.scroll(at: CGPoint(x: 35, y: 150), delta: 45.25); try emit("category-scroll")
        canvas.perform("category:\(categories[2].id)"); try emit("category-filter")
        canvas.perform("uncategorized"); try emit("uncategorized")
        canvas.perform("category:\(categories[13].id)"); try emit("category-last")
        canvas.perform("all"); controller.select(UUID(uuidString: "B0000000-0000-4000-8000-000000000001")); canvas.render(animated: true); try emit("document-empty-title")
        controller.select(UUID(uuidString: "B0000000-0000-4000-8000-000000000002")); canvas.render(); try emit("document-empty-body")
        controller.select(UUID(uuidString: "B0000000-0000-4000-8000-000000000003")); canvas.render(); canvas.formattingField = "body"; try emit("document-format")
        canvas.formattingColor = NSColor(srgbRed: 0.2, green: 0.6, blue: 0.7, alpha: 0.8); canvas.render(); try emit("document-format-color")
        canvas.formattingField = nil
        controller.select(UUID(uuidString: "B0000000-0000-4000-8000-000000000004")); canvas.render(); try emit("document-image-controls")
        controller.select(UUID(uuidString: "B0000000-0000-4000-8000-000000000005")); canvas.render(); try emit("document-video-controls")
        canvas.statusMessage = "Synthetic unavailable reference"; canvas.render(); try emit("document-error")
        configuration.theme = .light; HUDRuntimeAppearance.configuration = configuration
        _ = canvas.makeContent(for: .archive, style: .init(dark: false, accent: configuration.accentColor, contentsScale: 2)); try emit("document-light")
        canvas.statusMessage = nil; controller.select(nil); canvas.render(); try emit("gallery-light")
        let categoryMenu = endfieldArchiveReferenceCategoryMenu(categories, dark: false)
        try emit("category-menu", kind: "categoryMenu", menu: categoryMenu)
        endfieldArchiveReferenceScroll(categoryMenu, delta: 44.5); try emit("category-menu-scroll", kind: "categoryMenu", menu: categoryMenu)
        categoryMenu.perform("newCategory"); try emit("category-menu-create", kind: "categoryMenu", menu: categoryMenu)
        try emit("delete-document-menu", kind: "deleteDocumentMenu", menu: endfieldArchiveReferenceChoiceMenu(category: false, dark: true))
        try emit("delete-category-menu", kind: "deleteCategoryMenu", menu: endfieldArchiveReferenceChoiceMenu(category: true, dark: false))
        try require(NSApp.windows.isEmpty && canvas.presentation == nil, "Detached export must not create windows or media playback")
        let unsupported = encoder.unsupported.filter { $0["category"] != "metadata-only-animation" && $0["feature"] != "layer subclass HUDControlHighlightLayer" }
        try write(["windowCount": NSApp.windows.count, "sourceMetadataLimitations": encoder.unsupported], output.appendingPathComponent("export-diagnostics.json"))
        try require(unsupported.isEmpty, "Unexpected source layer feature")
        try write(["schemaVersion": 1, "entries": rows, "rasterAssets": encoder.rasterAssets, "windowCreated": false, "mediaOpened": false, "sourceMetadataLimitations": encoder.unsupported,
            "limitations": ["Original model layers and action/clip/scroll geometry. Native font raster, intermediate CoreAnimation pixels and projected editor layout are separate gates.", "Media descriptors are inert synthetic references; no thumbnail decode or player is activated."]], output.appendingPathComponent("archive-reference.json"))
    }
    static func main() { do { try run(); print("PASS detached actual-source Archive reference") } catch { fputs("Archive source export failed: \(error)\n", stderr); exit(1) } }
}
