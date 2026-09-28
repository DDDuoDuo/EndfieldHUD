import Foundation
import CoreGraphics
import ImageIO
import SQLite3

enum NotesStoreTests {
    static func run() -> Int {
        var count = 0
        func check(_ condition: Bool, _ message: String, file: StaticString = #file, line: UInt = #line) {
            count += 1
            if !condition { fatalError(message, file: file, line: line) }
        }
        func rejected(_ action: () throws -> Void) -> Bool {
            do { try action(); return false } catch { return true }
        }
        let root = FileManager.default.temporaryDirectory.appendingPathComponent("NotesStoreTests-\(UUID().uuidString)", isDirectory: true)
        defer { try? FileManager.default.removeItem(at: root) }
        do {
            let location = root.appendingPathComponent("roundtrip", isDirectory: true)
            let store = try NotesStore(directory: location)
            check(store.notes.isEmpty, "A fresh Notes store is empty")
            check(NotesStore.applicationDirectory() == NotesStore.applicationDirectory(),
                  "Store location remains stable within a process across HUD reopening")
            let created = Date(timeIntervalSince1970: 1_760_000_000.25)
            let text = CanvasNote(kind: .text, text: "便签\nA quote: ' and a NUL: \0 end", x: 72, y: 83,
                                  width: 177, height: 104, zIndex: 7, createdAt: created, isPinned: true)
            let items = [NoteChecklistItem(text: "第一项", isChecked: false),
                         NoteChecklistItem(text: "Second", isChecked: true),
                         NoteChecklistItem(text: "Move me", isChecked: false)]
            var todo = CanvasNote(kind: .todo, text: "Checklist title", items: items,
                                  x: 100, y: 142, width: 190, height: 140, zIndex: 2,
                                  createdAt: created.addingTimeInterval(1))
            try store.upsert(text)
            try store.upsert(todo)
            check(store.notes == [todo, text], "Canvas notes are kept in persisted z-order")
            let reopened = try NotesStore(directory: location)
            check(reopened.notes == [todo, text],
                  "Every field, Unicode, embedded NUL, pin and checklist identity survives reopening")
            todo.items[0].isChecked = true
            todo.items.swapAt(0, 2)
            todo.items.remove(at: 1)
            todo.items.append(NoteChecklistItem(text: "Added"))
            todo.x = 150; todo.y = 60; todo.width = 200; todo.height = 180; todo.zIndex = 10
            try store.upsert(todo)
            let reordered = try NotesStore(directory: location)
            check(reordered.notes == [text, todo], "Checklist reorder, deletion, check state and resized geometry round-trip")
            check(reordered.notes[1].items.map(\.id) == todo.items.map(\.id), "Checklist operations preserve stable item IDs")
            try store.delete(id: text.id)
            let deleted = try NotesStore(directory: location)
            check(store.notes == [todo] && deleted.notes == [todo], "Deleting a note updates memory and the committed database")
            try store.delete(id: UUID())
            check(store.notes == [todo], "Deleting a missing note leaves all existing notes intact")

            let bounded = NotesGeometry.constrained(CanvasNote(kind: .text, x: -999, y: 999, width: 0, height: 999), in: NotesGeometry.canvas)
            check(bounded.x == 8 && bounded.y == 34 && bounded.width == 110 && bounded.height == 248,
                  "Note geometry respects the minimum size and drawable canvas bounds")
            let invalid = NotesGeometry.constrained(CanvasNote(kind: .text, x: .nan, y: .infinity, width: .nan, height: -.infinity), in: NotesGeometry.canvas)
            check(invalid.x == 24 && invalid.y == 50 && invalid.width == 160 && invalid.height == 110,
                  "Nonfinite geometry is normalized before reaching the canvas or SQLite")
            let oversized = NotesGeometry.constrained(CanvasNote(kind: .text, x: 999, y: -999, width: 999, height: -99), in: NotesGeometry.canvas)
            check(oversized.x == 8 && oversized.y == 34 && oversized.width == 384 && oversized.height == 70,
                  "Large notes and negative heights cannot extend beyond the canvas")
            let smallTodo = NotesGeometry.constrained(CanvasNote(kind: .todo, width: 1, height: 1))
            check(smallTodo.width == 180 && smallTodo.height == 105,
                  "Minimum checklist dimensions leave space for checkbox, text, reorder, delete and footer controls")
            try store.upsert(bounded)
            check(store.notes.contains(bounded), "Persistence writes the same constrained geometry returned to the canvas")

            let spatial = CanvasNote(kind: .text, text: "Far across the screen", x: 1350, y: 820,
                                     width: 400, height: 270, zIndex: 12, isPinned: true)
            try store.upsert(spatial)
            let spatialReload = try NotesStore(directory: location)
            check(spatialReload.notes.contains(spatial), "Persistence preserves screen-wide geometry without shrinking it into the old module box")
            try store.delete(id: spatial.id)

            let imageBytes = try testImageData(width: 2400, height: 1200)
            let imagePath = root.appendingPathComponent("source.png")
            try imageBytes.write(to: imagePath)
            let imageNote = try store.importImage(from: imagePath, at: CGPoint(x: 500, y: 500))
            let asset = store.imageURL(for: imageNote)
            check(asset != nil && asset?.lastPathComponent == imageNote.imageName && asset != imagePath,
                  "Image imports make an owned asset rather than saving an external reference")
            try FileManager.default.removeItem(at: imagePath)
            check(asset.map { FileManager.default.fileExists(atPath: $0.path) } == true,
                  "Removing the original image does not break a note")
            let importedReopened = try NotesStore(directory: location)
            check(importedReopened.notes.contains(imageNote) && importedReopened.imageURL(for: imageNote) == asset,
                  "Image notes reopen with all geometry and the managed image reference intact")
            if let asset, let source = CGImageSourceCreateWithURL(asset as CFURL, nil),
               let properties = CGImageSourceCopyPropertiesAtIndex(source, 0, nil) as? [CFString: Any] {
                check((properties[kCGImagePropertyPixelWidth] as? Int) == 1600
                      && (properties[kCGImagePropertyPixelHeight] as? Int) == 800,
                      "Large image data is decoded once into a bounded 1600-pixel managed PNG")
            } else { check(false, "The managed PNG must be readable by ImageIO") }
            check(imageNote.kind == .image && imageNote.zIndex > todo.zIndex && NotesGeometry.constrained(imageNote) == imageNote,
                  "Imported images enter the front of the canvas with valid geometry")
            let dataImage = try store.importImage(data: imageBytes, at: CGPoint(x: 9, y: 35))
            check(dataImage.id != imageNote.id && dataImage.imageName != imageNote.imageName,
                  "Pasted image data receives independent identity and owned storage")
            let beforeInvalidImage = store.notes
            let beforeFiles = try FileManager.default.contentsOfDirectory(atPath: location.appendingPathComponent("Images").path)
            check(rejected { _ = try store.importImage(data: Data("This is not an image".utf8), at: .zero) },
                  "Non-image drag data is rejected")
            let afterInvalidFiles = try FileManager.default.contentsOfDirectory(atPath: location.appendingPathComponent("Images").path)
            check(store.notes == beforeInvalidImage && afterInvalidFiles == beforeFiles,
                  "Rejected image data cannot create a note or leave an asset behind")
            var unsafe = imageNote
            unsafe.imageName = "../../outside.png"
            check(rejected { try store.upsert(unsafe) } && store.imageURL(for: unsafe) == nil,
                  "Path traversal cannot read, overwrite or remove files outside managed images")
            var missing = imageNote
            missing.imageName = UUID().uuidString + ".png"
            check(rejected { try store.upsert(missing) }, "A new note cannot reference a missing asset")
            var duplicateChecklist = todo
            duplicateChecklist.items.append(duplicateChecklist.items[0])
            check(rejected { try store.upsert(duplicateChecklist) }, "Duplicate checklist identity is rejected before a write")

            // Force real SQLite write failures, including asset import and delete,
            // and verify both memory and disk preserve the last committed state.
            try sql("CREATE TRIGGER reject_update BEFORE UPDATE ON notes BEGIN SELECT RAISE(ABORT, 'test failure'); END", at: location)
            var changed = todo
            changed.text = "Must not commit"
            let beforeFailure = store.notes
            check(rejected { try store.upsert(changed) }, "SQLite update failures reach the caller")
            let afterFailedUpdate = try NotesStore(directory: location)
            check(store.notes == beforeFailure && afterFailedUpdate.notes == beforeFailure,
                  "Failed updates roll back without changing the in-memory or persisted note")
            try sql("DROP TRIGGER reject_update", at: location)
            try sql("CREATE TRIGGER reject_insert BEFORE INSERT ON notes BEGIN SELECT RAISE(ABORT, 'test failure'); END", at: location)
            let filesBeforeFailedImport = Set(try FileManager.default.contentsOfDirectory(atPath: location.appendingPathComponent("Images").path))
            check(rejected { _ = try store.importImage(data: imageBytes, at: .zero) }, "A failed image-note commit is reported")
            let filesAfterFailedImport = Set(try FileManager.default.contentsOfDirectory(atPath: location.appendingPathComponent("Images").path))
            check(store.notes == beforeFailure && filesAfterFailedImport == filesBeforeFailedImport,
                  "Failed image imports delete only the newly generated asset and retain all existing notes")
            try sql("DROP TRIGGER reject_insert", at: location)
            try sql("CREATE TRIGGER reject_delete BEFORE DELETE ON notes BEGIN SELECT RAISE(ABORT, 'test failure'); END", at: location)
            check(rejected { try store.delete(id: imageNote.id) }, "Failed deletion reaches the caller")
            check(store.notes == beforeFailure && store.imageURL(for: imageNote) == asset,
                  "A failed deletion preserves the note and its image file")
            try sql("DROP TRIGGER reject_delete", at: location)

            var sharedImage = imageNote
            sharedImage.id = UUID()
            sharedImage.zIndex += 10
            try store.upsert(sharedImage)
            try store.delete(id: imageNote.id)
            check(store.imageURL(for: sharedImage) == asset,
                  "Deleting a note cannot remove an image still referenced by another note")
            try store.delete(id: sharedImage.id)
            check(asset.map { !FileManager.default.fileExists(atPath: $0.path) } == true,
                  "A committed deletion removes an image once its final reference is gone")
            var replaced = dataImage
            let replacedURL = store.imageURL(for: replaced)
            replaced.kind = .text; replaced.imageName = nil; replaced.text = "Converted"
            try store.upsert(replaced)
            check(replacedURL.map { !FileManager.default.fileExists(atPath: $0.path) } == true,
                  "A committed replacement cleans up only the now-unreferenced image")

            let corrupt = root.appendingPathComponent("corrupt", isDirectory: true)
            try FileManager.default.createDirectory(at: corrupt, withIntermediateDirectories: true)
            let corruptPath = corrupt.appendingPathComponent("notes.sqlite3")
            let corruptBytes = Data("not a sqlite database — preserve these bytes".utf8)
            try corruptBytes.write(to: corruptPath)
            check(rejected { _ = try NotesStore(directory: corrupt) }, "A corrupt database raises an error instead of resetting")
            check(try Data(contentsOf: corruptPath) == corruptBytes, "Corrupt database bytes are not overwritten or removed")

            let future = root.appendingPathComponent("future", isDirectory: true)
            _ = try NotesStore(directory: future)
            try sql("PRAGMA user_version = 99", at: future)
            let futureBytes = try Data(contentsOf: future.appendingPathComponent("notes.sqlite3"))
            check(rejected { _ = try NotesStore(directory: future) }, "A future schema is not opened for writing by an older app")
            check(try Data(contentsOf: future.appendingPathComponent("notes.sqlite3")) == futureBytes,
                  "Unsupported future databases are preserved byte-for-byte")

            let invalidRecords = root.appendingPathComponent("invalid-records", isDirectory: true)
            let invalidStore = try NotesStore(directory: invalidRecords)
            try invalidStore.upsert(text)
            try sql("UPDATE notes SET checklist = 'broken JSON'", at: invalidRecords)
            let invalidBytes = try Data(contentsOf: invalidRecords.appendingPathComponent("notes.sqlite3"))
            check(rejected { _ = try NotesStore(directory: invalidRecords) }, "Invalid saved content is reported rather than silently dropped")
            check(try Data(contentsOf: invalidRecords.appendingPathComponent("notes.sqlite3")) == invalidBytes,
                  "Malformed note content is preserved for recovery")
        } catch { fatalError("Notes persistence test failed: \(error)") }
        return count
    }

    private static func sql(_ statement: String, at directory: URL) throws {
        var database: OpaquePointer?
        guard sqlite3_open(directory.appendingPathComponent("notes.sqlite3").path, &database) == SQLITE_OK,
              let database else { throw NotesStoreError.database("Test connection failed") }
        defer { sqlite3_close(database) }
        guard sqlite3_exec(database, statement, nil, nil, nil) == SQLITE_OK else {
            throw NotesStoreError.database(String(cString: sqlite3_errmsg(database)))
        }
    }

    private static func testImageData(width: Int, height: Int) throws -> Data {
        guard let context = CGContext(data: nil, width: width, height: height, bitsPerComponent: 8,
                                      bytesPerRow: width * 4, space: CGColorSpaceCreateDeviceRGB(),
                                      bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else {
            throw NotesStoreError.invalidImage
        }
        context.setFillColor(CGColor(red: 0.2, green: 0.7, blue: 0.4, alpha: 1))
        context.fill(CGRect(x: 0, y: 0, width: width, height: height))
        let data = NSMutableData()
        guard let image = context.makeImage(),
              let destination = CGImageDestinationCreateWithData(data, "public.png" as CFString, 1, nil) else {
            throw NotesStoreError.invalidImage
        }
        CGImageDestinationAddImage(destination, image, nil)
        guard CGImageDestinationFinalize(destination) else { throw NotesStoreError.invalidImage }
        return data as Data
    }
}
