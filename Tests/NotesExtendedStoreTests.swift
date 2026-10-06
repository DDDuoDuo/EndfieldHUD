import Foundation
import CoreGraphics
import SQLite3

enum NotesExtendedStoreTests {
    static func run() -> Int {
        var count = 0
        func check(_ value: Bool, _ message: String) { count += 1; precondition(value, message) }
        let root = FileManager.default.temporaryDirectory.appendingPathComponent("NotesExtendedStoreTests-\(UUID().uuidString)")
        defer { try? FileManager.default.removeItem(at: root) }
        func sql(_ value: String, at directory: URL) throws {
            try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
            var db: OpaquePointer?
            guard sqlite3_open(directory.appendingPathComponent("notes.sqlite3").path, &db) == SQLITE_OK else { throw NotesStoreError.invalidRecord }
            defer { sqlite3_close(db) }
            guard sqlite3_exec(db, value, nil, nil, nil) == SQLITE_OK else { throw NotesStoreError.invalidRecord }
        }
        func rejects(_ action: () throws -> Void) -> Bool { do { try action(); return false } catch { return true } }
        do {
            let legacy = root.appendingPathComponent("legacy")
            let id = UUID()
            try sql("""
                CREATE TABLE notes (id TEXT PRIMARY KEY NOT NULL, kind TEXT NOT NULL CHECK(kind IN ('text','todo','image')),
                text TEXT NOT NULL, checklist TEXT NOT NULL, image_name TEXT, x REAL NOT NULL, y REAL NOT NULL,
                width REAL NOT NULL, height REAL NOT NULL, z_index INTEGER NOT NULL, created_at REAL NOT NULL,
                is_pinned INTEGER NOT NULL CHECK(is_pinned IN (0,1)));
                INSERT INTO notes VALUES ('\(id.uuidString)','text','原有文字 🐈','[]',NULL,40,55,220,170,7,765432100.25,1);
                PRAGMA user_version=1;
                """, at: legacy)
            let migrated = try NotesStore(directory: legacy)
            let original = migrated.notes[0]
            check(original.id == id && original.text == "原有文字 🐈" && original.x == 40
                && original.y == 55 && original.width == 220 && original.height == 170
                && original.zIndex == 7 && original.isPinned && original.richText == nil && original.media == nil && original.drawing == nil,
                  "The additive v1 migration preserves all legacy plain-note identity, content, geometry and pin fields")
            var styled = original
            var style = NotesTextStyle(); style.fontSize = 28; style.bold = true
            styled.richText = NotesRichText(runs: [NotesTextRun(location: 0, length: 4, style: style)])
            try migrated.upsert(styled)
            let drawing = CanvasNote(kind: .drawing, x: 410, y: 160, width: 300, height: 250, isPinned: true,
                drawing: NotesDrawing(strokes: [NotesDrawingStroke(points: [NotesDrawingPoint(x: 0.2, y: 0.3)], width: 12,
                    color: NotesRGBA(red: 1, green: 0, blue: 0))]))
            try migrated.upsert(drawing)
            let reference = NotesMediaReference(kind: .video, bookmark: Data([1, 2, 3]), isSecurityScoped: false,
                lastKnownPath: "/synthetic/not-opened.mp4", displayName: "Example.mp4", pixelWidth: 640, pixelHeight: 480, duration: 1, frameCount: 1)
            let media = try migrated.importMedia(reference: reference, at: CGPoint(x: 180, y: 200))
            check(media.imageName == nil && media.media == reference, "Media persistence stores a small descriptor without copying a movie")
            let reopened = try NotesStore(directory: legacy)
            check(reopened.notes.contains(styled) && reopened.notes.contains(drawing) && reopened.notes.contains(media),
                  "Rich text, drawing and media reopen beside legacy records with unchanged IDs and pins")
            check(try FileManager.default.contentsOfDirectory(atPath: legacy.appendingPathComponent("Images").path).isEmpty,
                  "Bookmark media persistence never creates a duplicate file in managed images")
            let before = migrated.notes
            var invalid = styled; invalid.richText?.runs[0].length = Int.max
            check(rejects { try migrated.upsert(invalid) } && migrated.notes == before,
                  "Invalid attribute ranges reject before any SQLite or memory change")
            var badDrawing = drawing; badDrawing.drawing?.strokes[0].points[0].x = .infinity
            check(rejects { try migrated.upsert(badDrawing) }, "Invalid drawing coordinates reject before persistence")
            var badMedia = media; badMedia.media?.bookmark = Data()
            check(rejects { try migrated.upsert(badMedia) }, "Invalid bookmark payloads are rejected without filesystem access")
            try sql("CREATE TRIGGER reject_payload BEFORE UPDATE ON notes BEGIN SELECT RAISE(ABORT,'test'); END;", at: legacy)
            var edited = styled; edited.richText?.runs[0].style.fontSize = 40
            check(rejects { try migrated.upsert(edited) } && migrated.notes == before,
                  "Failed rich-text commits preserve the previous in-memory note")
            check(try NotesStore(directory: legacy).notes == reopened.notes,
                  "A failed payload commit preserves all original rows and payloads on disk")

            let corrupt = root.appendingPathComponent("corrupt-v1")
            try FileManager.default.copyItem(at: legacy, to: corrupt)
            try sql("DROP TRIGGER reject_payload; UPDATE notes SET checklist='invalid' WHERE id='\(id.uuidString)'; PRAGMA user_version=1;", at: corrupt)
            let bytes = try Data(contentsOf: corrupt.appendingPathComponent("notes.sqlite3"))
            check(rejects { _ = try NotesStore(directory: corrupt) }, "Corrupt pre-migration rows abort additive migration")
            check(try Data(contentsOf: corrupt.appendingPathComponent("notes.sqlite3")) == bytes,
                  "A failed migration rolls back without replacing the original database")
            let futurePayload = root.appendingPathComponent("future-payload")
            let payloadStore = try NotesStore(directory: futurePayload)
            try payloadStore.upsert(drawing)
            try sql("UPDATE notes SET drawing='{" + "\"version\":99,\"strokes\":[]}'", at: futurePayload)
            let futureBytes = try Data(contentsOf: futurePayload.appendingPathComponent("notes.sqlite3"))
            check(rejects { _ = try NotesStore(directory: futurePayload) }, "Unknown payload versions fail closed even inside a current database schema")
            check(try Data(contentsOf: futurePayload.appendingPathComponent("notes.sqlite3")) == futureBytes,
                  "Unknown payload data is preserved byte-for-byte for a future application")
        } catch { preconditionFailure("Notes payload fixture failed: \(error)") }
        return count
    }
}
