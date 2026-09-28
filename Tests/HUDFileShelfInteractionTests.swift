import AppKit

/// Uses a private pasteboard and a windowless view: no general clipboard,
/// native drag loop, chooser, preview panel or Finder interaction is involved.
enum HUDFileShelfInteractionTests {
    static func run() -> Int {
        var count = 0
        func check(_ condition: Bool, _ message: String, file: StaticString = #file, line: UInt = #line) {
            count += 1
            if !condition { fatalError(message, file: file, line: line) }
        }
        let manager = FileManager.default
        let directory = manager.temporaryDirectory.appendingPathComponent("HUDFileShelfInteractionTests-\(UUID().uuidString)", isDirectory: true)
        let pasteboard = NSPasteboard(name: NSPasteboard.Name("EndfieldCharge.FileShelfTests.\(UUID().uuidString)"))
        defer {
            pasteboard.releaseGlobally()
            try? manager.removeItem(at: directory)
        }
        do {
            let originals = directory.appendingPathComponent("originals", isDirectory: true)
            try manager.createDirectory(at: originals, withIntermediateDirectories: true)
            let first = originals.appendingPathComponent("Space and 'quote'.txt")
            let second = originals.appendingPathComponent("中文.pdf")
            let folder = originals.appendingPathComponent("Folder", isDirectory: true)
            let nested = folder.appendingPathComponent("nested.txt")
            let firstBytes = Data("First original contents\n".utf8)
            let secondBytes = Data("Second original contents\0\n".utf8)
            let nestedBytes = Data("Keep folder contents untouched.\n".utf8)
            try firstBytes.write(to: first)
            try secondBytes.write(to: second)
            try manager.createDirectory(at: folder, withIntermediateDirectories: true)
            try nestedBytes.write(to: nested)
            let urls = [first, second, folder]
            let metadata = directory.appendingPathComponent("metadata", isDirectory: true)
            let store = try FileShelfStore(directory: metadata)
            let canvas = FileShelfCanvas(store: store)
            let host = NSView(frame: CGRect(x: 0, y: 0, width: 400, height: 334))
            let bridge = HUDFileShelfInteraction(canvas: canvas, store: store, host: host)
            defer { bridge.deactivate() }

            pasteboard.clearContents()
            check(!HUDFileShelfInteraction.acceptsFiles(pasteboard), "An empty private pasteboard is not a file drop")
            check(pasteboard.writeObjects(urls.map { $0 as NSURL }), "Native NSURL pasteboard writers advertise the test file references")
            check(HUDFileShelfInteraction.acceptsFiles(pasteboard), "File and directory URLs are accepted as shelf references")
            check(!bridge.importPasteboard(pasteboard) && store.items.isEmpty,
                  "An inactive shelf does not import otherwise valid file references")
            bridge.setActive(true)
            check(bridge.importPasteboard(pasteboard), "An active bridge imports multiple native file URL objects")
            check(store.items.count == urls.count && canvas.itemCount == urls.count,
                  "Every incoming file or folder becomes exactly one shelf card")
            check(store.items.map(\.name) == urls.map(\.lastPathComponent),
                  "Incoming pasteboard order, spaces, apostrophes and Unicode filenames are retained")
            check(store.items.last?.isDirectory == true,
                  "An incoming folder remains a directory reference rather than a copied payload")
            let reopened = try FileShelfStore(directory: metadata)
            check(reopened.items == store.items, "References imported through AppKit persist across store reopening")
            check(try Data(contentsOf: first) == firstBytes && Data(contentsOf: second) == secondBytes && Data(contentsOf: nested) == nestedBytes,
                  "Importing pasteboard references never modifies or removes the original files or folder contents")
            check(try manager.contentsOfDirectory(atPath: metadata.path) == ["shelf.json"],
                  "Importing file URLs stores only bookmark metadata, with no source-file copies")

            let beforeDuplicate = store.items
            let archive = metadata.appendingPathComponent("shelf.json")
            let archiveBeforeDuplicate = try Data(contentsOf: archive)
            check(bridge.importPasteboard(pasteboard) && store.items == beforeDuplicate,
                  "A repeated native drop is accepted without duplicating existing shelf references")
            check(try Data(contentsOf: archive) == archiveBeforeDuplicate,
                  "A duplicate drop does not rewrite persisted metadata")

            // This is the same NSURL writer class the native outgoing session
            // receives; verify its bytes can be consumed by another URL reader.
            let access = try store.access(id: store.items[0].id)
            defer { access.close() }
            pasteboard.clearContents()
            check(pasteboard.writeObjects([access.url as NSURL]), "The native outgoing URL writer can populate a receiving pasteboard")
            let outgoing = pasteboard.readObjects(forClasses: [NSURL.self], options: [.urlReadingFileURLsOnly: true]) as? [URL] ?? []
            check(outgoing.count == 1 && outgoing.first?.standardizedFileURL.resolvingSymlinksInPath() == first.standardizedFileURL.resolvingSymlinksInPath(),
                  "A receiving app reads the original existing file URL from the outgoing writer")
            check(HUDFileShelfInteraction.acceptsFiles(pasteboard), "An outgoing shelf URL retains the standard native file URL type")

            let firstCard = canvas.cardRect(for: store.items[0].id)!
            let lastCard = canvas.cardRect(for: store.items[2].id)!
            let plainEvent = NSEvent.mouseEvent(with: .leftMouseDown, location: .zero, modifierFlags: [], timestamp: 0,
                                               windowNumber: 0, context: nil, eventNumber: 1, clickCount: 1, pressure: 1)!
            let rangeEvent = NSEvent.mouseEvent(with: .leftMouseDown, location: .zero, modifierFlags: [.shift], timestamp: 0,
                                               windowNumber: 0, context: nil, eventNumber: 2, clickCount: 1, pressure: 1)!
            check(bridge.mouseDown(at: CGPoint(x: firstCard.minX + 18, y: firstCard.minY + 20), event: plainEvent),
                  "The native interaction accepts a normal shelf card selection")
            bridge.mouseUp()
            check(bridge.mouseDown(at: CGPoint(x: lastCard.minX + 18, y: lastCard.minY + 20), event: rangeEvent),
                  "The native interaction accepts Shift-click range selection")
            bridge.mouseUp()
            check(canvas.selectedIDs == Set(store.items.map(\.id)), "Native modifier flags reach the canvas and select all three references")
            let groupIDs = canvas.dragSelection(primaryID: store.items[1].id)
            let groupAccess = try groupIDs.map { try store.access(id: $0) }
            defer { groupAccess.forEach { $0.close() } }
            pasteboard.clearContents()
            check(pasteboard.writeObjects(groupAccess.map { $0.url as NSURL }), "A selected group exports multiple standard NSURL writers together")
            let outgoingGroup = pasteboard.readObjects(forClasses: [NSURL.self], options: [.urlReadingFileURLsOnly: true]) as? [URL] ?? []
            check(outgoingGroup.map { $0.standardizedFileURL.resolvingSymlinksInPath() } == urls.map { $0.standardizedFileURL.resolvingSymlinksInPath() },
                  "A receiving app reads the complete ordered selection, including the folder, without copied source contents")
            check(!bridge.isInputLocked, "Mouse-up releases both plain-click and Shift-click pending drag locks")

            var revealAccess: ShelfFileAccess?
            bridge.onRevealRequested = { revealAccess = $0 }
            canvas.perform(actionID: "shelf:\(store.items[0].id.uuidString):reveal")
            check(revealAccess?.url.standardizedFileURL.resolvingSymlinksInPath() == first.standardizedFileURL.resolvingSymlinksInPath(),
                  "Reveal transfers the original reference lease to the owner instead of opening Finder inside the interaction")
            bridge.deactivate()
            let retainedBytes = try revealAccess.map { try Data(contentsOf: $0.url) }
            check(revealAccess != nil && retainedBytes == firstBytes,
                  "The queued Finder handoff retains access while the shelf deactivates for its closing animation")
            revealAccess?.close(); revealAccess = nil
            bridge.setActive(true)
            check(store.items.map(\.id) == beforeDuplicate.map(\.id),
                  "Reopening after a reveal preserves every shelf reference")
            // Activation refreshes resolved paths/bookmarks before testing
            // unsupported drops; compare those drops against the fresh state.
            let beforeUnsupported = store.items

            pasteboard.clearContents()
            check(pasteboard.writeObjects([URL(string: "https://example.com/document.pdf")! as NSURL]),
                  "A non-file URL fixture is written without requesting the network")
            check(!HUDFileShelfInteraction.acceptsFiles(pasteboard) && !bridge.importPasteboard(pasteboard),
                  "Web URLs cannot be treated as local files")
            pasteboard.clearContents()
            pasteboard.setString(first.path, forType: .string)
            check(!HUDFileShelfInteraction.acceptsFiles(pasteboard) && !bridge.importPasteboard(pasteboard),
                  "Plain text that resembles a pathname is not silently converted into a file reference")
            pasteboard.clearContents()
            pasteboard.setString("public.data", forType: NSPasteboard.PasteboardType("com.apple.pasteboard.promised-file-content-type"))
            pasteboard.setString("promised.txt", forType: NSPasteboard.PasteboardType("com.apple.pasteboard.promised-file-name"))
            check(!HUDFileShelfInteraction.acceptsFiles(pasteboard) && !bridge.importPasteboard(pasteboard),
                  "Promised-only files are rejected without materializing or copying any promised contents")
            pasteboard.clearContents()
            let imageBytes = Data(base64Encoded: "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+sNf0AAAAASUVORK5CYII=")!
            pasteboard.setData(imageBytes, forType: .png)
            check(!HUDFileShelfInteraction.acceptsFiles(pasteboard) && !bridge.importPasteboard(pasteboard),
                  "Image bytes alone are not confused with a referenced Finder file")
            check(store.items == beforeUnsupported && canvas.itemCount == urls.count,
                  "Unsupported pasteboard types leave the shelf unchanged")

            bridge.setActive(false)
            pasteboard.clearContents()
            pasteboard.writeObjects([first as NSURL])
            check(!bridge.importPasteboard(pasteboard) && store.items == beforeUnsupported,
                  "Deactivating the shelf disables incoming native pasteboard imports")
            check(!bridge.isDraggingOut && !bridge.isPresentingPanel && !bridge.isInputLocked,
                  "Windowless imports leave no drag, modal panel or interaction lock behind")
            check(try Data(contentsOf: first) == firstBytes && Data(contentsOf: second) == secondBytes && Data(contentsOf: nested) == nestedBytes,
                  "Pasteboard validation, readback and deactivation preserve every original byte")
        } catch { fatalError("File shelf interaction test failed: \(error)") }
        return count
    }
}
