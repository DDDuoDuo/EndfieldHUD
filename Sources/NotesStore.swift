import Foundation
import CoreGraphics
import ImageIO
import SQLite3

enum NoteKind: String, Codable {
    case text, todo, image, drawing
}

struct NoteChecklistItem: Codable, Equatable {
    var id: UUID = UUID()
    var text: String
    var isChecked: Bool = false
}

struct CanvasNote: Identifiable, Equatable {
    var id: UUID
    var kind: NoteKind
    var text: String
    var items: [NoteChecklistItem]
    var imageName: String?
    var x: Double
    var y: Double
    var width: Double
    var height: Double
    var zIndex: Int
    var createdAt: Date
    var isPinned: Bool
    var richText: NotesRichText?
    var media: NotesMediaReference?
    var drawing: NotesDrawing?

    init(id: UUID = UUID(), kind: NoteKind, text: String = "", items: [NoteChecklistItem] = [],
         imageName: String? = nil, x: Double = 24, y: Double = 50, width: Double = 160,
         height: Double = 110, zIndex: Int = 0, createdAt: Date = Date(), isPinned: Bool = false,
         richText: NotesRichText? = nil, media: NotesMediaReference? = nil, drawing: NotesDrawing? = nil) {
        self.id = id; self.kind = kind; self.text = text; self.items = items; self.imageName = imageName
        self.x = x; self.y = y; self.width = width; self.height = height
        self.zIndex = zIndex; self.createdAt = createdAt; self.isPinned = isPinned
        self.richText = richText; self.media = media; self.drawing = drawing
    }
}

enum NotesGeometry {
    static let canvas = CGRect(x: 8, y: 34, width: 384, height: 248)

    /// Persistence validates geometry without tying it to one HUD or display size.
    /// Only an interactive gesture supplies the current workspace bounds.
    static func constrained(_ note: CanvasNote, in bounds: CGRect? = nil) -> CanvasNote {
        var result = note
        let minimumWidth = note.kind == .todo ? 180.0 : 110.0
        let minimumHeight = note.kind == .todo ? 105.0 : 70.0
        result.width = min(32_768, max(minimumWidth, note.width.isFinite ? note.width : 160))
        result.height = min(32_768, max(minimumHeight, note.height.isFinite ? note.height : 110))
        result.x = note.x.isFinite ? min(1_000_000, max(-1_000_000, note.x)) : 24
        result.y = note.y.isFinite ? min(1_000_000, max(-1_000_000, note.y)) : 50
        if let bounds, bounds.width > 0, bounds.height > 0,
           [bounds.minX, bounds.minY, bounds.width, bounds.height].allSatisfy({ $0.isFinite }) {
            result.width = min(Double(bounds.width), result.width)
            result.height = min(Double(bounds.height), result.height)
            result.x = min(Double(bounds.maxX) - result.width, max(Double(bounds.minX), result.x))
            result.y = min(Double(bounds.maxY) - result.height, max(Double(bounds.minY), result.y))
        }
        return result
    }

}

enum NotesStoreError: LocalizedError {
    case database(String)
    case invalidRecord
    case newerDatabase
    case invalidImage
    case imageTooLarge
    case invalidImageName

    var errorDescription: String? {
        switch self {
        case .database(let detail): return L10n.text("Notes could not be saved or opened: ", "无法保存或打开便笺：") + detail
        case .invalidRecord: return L10n.text("A saved note could not be read. The original database has been preserved.", "无法读取已保存的便笺，原始数据库已保留。")
        case .newerDatabase: return L10n.text("These notes were saved by a newer version of EndfieldCharge.", "这些便笺由较新版本的 EndfieldCharge 保存。")
        case .invalidImage: return L10n.text("This file could not be opened as an image.", "无法将此文件作为图片打开。")
        case .imageTooLarge: return L10n.text("Choose an image smaller than 128 MB.", "请选择小于 128 MB 的图片。")
        case .invalidImageName: return L10n.text("The note contains an invalid image reference.", "便笺中的图片引用无效。")
        }
    }
}

/// A single main-thread owner writes only after an edit, never on a timer or on a
/// drawing frame. In-memory state changes only after the SQLite commit succeeds.
final class NotesStore {
    private(set) var notes: [CanvasNote] = []
    private var database: OpaquePointer?
    private let directory: URL
    private let imagesDirectory: URL
    private let fileManager = FileManager.default
    private let transient = unsafeBitCast(-1, to: sqlite3_destructor_type.self)
    private static let diagnosticDirectory = FileManager.default.temporaryDirectory
        .appendingPathComponent("EndfieldCharge-Notes-\(ProcessInfo.processInfo.processIdentifier)-\(UUID().uuidString)", isDirectory: true)

    static func applicationDirectory() -> URL {
        let diagnostic = CommandLine.arguments.contains { argument in
            argument == "--ui-test" || argument.hasSuffix("smoke-test") || argument.hasPrefix("--render-")
        }
        if diagnostic {
            return diagnosticDirectory
        }
        let support = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask).first
            ?? FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent("Library/Application Support", isDirectory: true)
        return support.appendingPathComponent("EndfieldCharge/Notes", isDirectory: true)
    }

    init(directory: URL) throws {
        self.directory = directory
        imagesDirectory = directory.appendingPathComponent("Images", isDirectory: true)
        try fileManager.createDirectory(at: directory, withIntermediateDirectories: true)
        let path = directory.appendingPathComponent("notes.sqlite3").path
        var opened: OpaquePointer?
        let result = sqlite3_open_v2(path, &opened, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nil)
        database = opened
        guard result == SQLITE_OK else {
            let detail = opened.map { String(cString: sqlite3_errmsg($0)) } ?? "Could not open the database."
            if let opened { sqlite3_close_v2(opened) }
            database = nil
            throw NotesStoreError.database(detail)
        }
        do {
            sqlite3_busy_timeout(database, 250)
            // Validate before any schema or journal writes. A damaged database is
            // reported to the UI; it is never silently replaced with an empty one.
            try withStatement("PRAGMA quick_check") { statement in
                guard sqlite3_step(statement) == SQLITE_ROW, string(statement, 0) == "ok" else {
                    throw NotesStoreError.invalidRecord
                }
            }
            var version: Int32 = 0
            try withStatement("PRAGMA user_version") { statement in
                guard sqlite3_step(statement) == SQLITE_ROW else { throw databaseError() }
                version = sqlite3_column_int(statement, 0)
            }
            guard version <= 2 else { throw NotesStoreError.newerDatabase }
            try transaction {
                try execute("""
                    CREATE TABLE IF NOT EXISTS notes (
                        id TEXT PRIMARY KEY NOT NULL,
                        kind TEXT NOT NULL CHECK(kind IN ('text','todo','image')),
                        text TEXT NOT NULL,
                        checklist TEXT NOT NULL,
                        image_name TEXT,
                        x REAL NOT NULL, y REAL NOT NULL,
                        width REAL NOT NULL, height REAL NOT NULL,
                        z_index INTEGER NOT NULL,
                        created_at REAL NOT NULL,
                        is_pinned INTEGER NOT NULL CHECK(is_pinned IN (0,1))
                    )
                    """)
                if version < 2 {
                    // Verify every original row before any migration commits.
                    _ = try readNotes(includePayloads: false)
                    try execute("ALTER TABLE notes ADD COLUMN rich_text TEXT")
                    try execute("ALTER TABLE notes ADD COLUMN media TEXT")
                    try execute("ALTER TABLE notes ADD COLUMN drawing TEXT")
                    try execute("PRAGMA user_version = 2")
                }
                notes = try readNotes()
            }
            try fileManager.createDirectory(at: imagesDirectory, withIntermediateDirectories: true)
        } catch {
            sqlite3_close_v2(database)
            database = nil
            throw error
        }
    }

    deinit { if let database { sqlite3_close_v2(database) } }

    func upsert(_ note: CanvasNote) throws {
        let note = NotesGeometry.constrained(note)
        guard note.createdAt.timeIntervalSinceReferenceDate.isFinite,
              Set(note.items.map(\.id)).count == note.items.count,
              note.richText.map({ note.kind == .text && $0.isValid(for: note.text) }) ?? true,
              note.media.map({ note.kind == .image && $0.isValid }) ?? true,
              note.drawing.map({ note.kind == .drawing && $0.isValid }) ?? (note.kind != .drawing)
        else { throw NotesStoreError.invalidRecord }
        if let name = note.imageName {
            guard validImageName(name), fileManager.fileExists(atPath: imagesDirectory.appendingPathComponent(name).path) else {
                throw NotesStoreError.invalidImageName
            }
        }
        guard note.kind != .image || note.imageName != nil || note.media != nil else { throw NotesStoreError.invalidImageName }
        let checklist = String(decoding: try JSONEncoder().encode(note.items), as: UTF8.self)
        func payload<T: Encodable>(_ value: T?) throws -> String? {
            try value.map { String(decoding: try JSONEncoder().encode($0), as: UTF8.self) }
        }
        let richText = try payload(note.richText), media = try payload(note.media), drawing = try payload(note.drawing)
        try transaction {
            try withStatement("""
                INSERT INTO notes (id,kind,text,checklist,image_name,x,y,width,height,z_index,created_at,is_pinned,rich_text,media,drawing)
                VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)
                ON CONFLICT(id) DO UPDATE SET kind=excluded.kind,text=excluded.text,checklist=excluded.checklist,
                    image_name=excluded.image_name,x=excluded.x,y=excluded.y,width=excluded.width,height=excluded.height,
                    z_index=excluded.z_index,created_at=excluded.created_at,is_pinned=excluded.is_pinned,
                    rich_text=excluded.rich_text,media=excluded.media,drawing=excluded.drawing
                """) { statement in
                try bind(note.id.uuidString, at: 1, to: statement)
                // Existing kind identifiers/check constraint remain byte-for-byte.
                // Drawing is identified by its versioned payload on the text base.
                try bind(note.kind == .drawing ? "text" : note.kind.rawValue, at: 2, to: statement)
                try bind(note.text, at: 3, to: statement)
                try bind(checklist, at: 4, to: statement)
                try bind(note.imageName, at: 5, to: statement)
                try checked(sqlite3_bind_double(statement, 6, note.x))
                try checked(sqlite3_bind_double(statement, 7, note.y))
                try checked(sqlite3_bind_double(statement, 8, note.width))
                try checked(sqlite3_bind_double(statement, 9, note.height))
                try checked(sqlite3_bind_int64(statement, 10, Int64(note.zIndex)))
                // Foundation's native epoch preserves the exact Date double;
                // converting through Unix time loses low bits on recent dates.
                try checked(sqlite3_bind_double(statement, 11, note.createdAt.timeIntervalSinceReferenceDate))
                try checked(sqlite3_bind_int(statement, 12, note.isPinned ? 1 : 0))
                try bind(richText, at: 13, to: statement)
                try bind(media, at: 14, to: statement)
                try bind(drawing, at: 15, to: statement)
                guard sqlite3_step(statement) == SQLITE_DONE else { throw databaseError() }
            }
        }
        let previousImage = notes.first { $0.id == note.id }?.imageName
        if let index = notes.firstIndex(where: { $0.id == note.id }) { notes[index] = note }
        else { notes.append(note) }
        sortNotes()
        if previousImage != note.imageName { removeImageIfUnused(previousImage) }
    }

    func delete(id: UUID) throws {
        let image = notes.first { $0.id == id }?.imageName
        try transaction {
            try withStatement("DELETE FROM notes WHERE id = ?") { statement in
                try bind(id.uuidString, at: 1, to: statement)
                guard sqlite3_step(statement) == SQLITE_DONE else { throw databaseError() }
            }
        }
        notes.removeAll { $0.id == id }
        removeImageIfUnused(image)
    }

    func imageURL(for note: CanvasNote) -> URL? {
        guard let name = note.imageName, validImageName(name) else { return nil }
        let url = imagesDirectory.appendingPathComponent(name)
        return fileManager.fileExists(atPath: url.path) ? url : nil
    }

    /// Descriptor preparation happens on the import worker; this commits only
    /// the small validated reference and never copies the source media file.
    func importMedia(reference: NotesMediaReference, at point: CGPoint, bounds: CGRect? = nil) throws -> CanvasNote {
        guard reference.isValid else { throw NotesStoreError.invalidRecord }
        let ratio = Double(reference.pixelHeight) / Double(max(1, reference.pixelWidth))
        let width = min(300.0, 210 / max(0.2, ratio))
        let nextZ = notes.map(\.zIndex).max() ?? -1
        let note = NotesGeometry.constrained(CanvasNote(kind: .image, x: Double(point.x), y: Double(point.y),
            width: max(162, width), height: max(110, width * ratio + 50),
            zIndex: nextZ < Int.max ? nextZ + 1 : nextZ, media: reference), in: bounds)
        try upsert(note)
        return note
    }

    func importImage(from url: URL, at point: CGPoint, bounds: CGRect? = nil) throws -> CanvasNote {
        guard url.isFileURL else { throw NotesStoreError.invalidImage }
        let scoped = url.startAccessingSecurityScopedResource()
        defer { if scoped { url.stopAccessingSecurityScopedResource() } }
        if let size = try url.resourceValues(forKeys: [.fileSizeKey]).fileSize, size > 128 * 1024 * 1024 {
            throw NotesStoreError.imageTooLarge
        }
        guard let source = CGImageSourceCreateWithURL(url as CFURL, [kCGImageSourceShouldCache: false] as CFDictionary) else {
            throw NotesStoreError.invalidImage
        }
        return try importImage(source: source, at: point, bounds: bounds)
    }

    func importImage(data: Data, at point: CGPoint, bounds: CGRect? = nil) throws -> CanvasNote {
        guard data.count <= 128 * 1024 * 1024 else { throw NotesStoreError.imageTooLarge }
        guard let source = CGImageSourceCreateWithData(data as CFData, [kCGImageSourceShouldCache: false] as CFDictionary) else {
            throw NotesStoreError.invalidImage
        }
        return try importImage(source: source, at: point, bounds: bounds)
    }

    private func importImage(source: CGImageSource, at point: CGPoint, bounds: CGRect?) throws -> CanvasNote {
        let options: [CFString: Any] = [kCGImageSourceCreateThumbnailFromImageAlways: true,
                                        kCGImageSourceCreateThumbnailWithTransform: true,
                                        kCGImageSourceThumbnailMaxPixelSize: 1600,
                                        kCGImageSourceShouldCacheImmediately: true]
        guard CGImageSourceGetCount(source) > 0,
              let image = CGImageSourceCreateThumbnailAtIndex(source, 0, options as CFDictionary) else {
            throw NotesStoreError.invalidImage
        }
        let encoded = NSMutableData()
        guard let destination = CGImageDestinationCreateWithData(encoded, "public.png" as CFString, 1, nil) else {
            throw NotesStoreError.invalidImage
        }
        CGImageDestinationAddImage(destination, image, nil)
        guard CGImageDestinationFinalize(destination) else { throw NotesStoreError.invalidImage }
        let name = UUID().uuidString + ".png"
        let url = imagesDirectory.appendingPathComponent(name)
        // Write the asset before committing its reference. A failed database
        // transaction only removes this newly generated, unreferenced asset.
        try (encoded as Data).write(to: url, options: .atomic)
        let ratio = Double(image.height) / Double(max(1, image.width))
        let width = min(210.0, 160 / max(0.1, ratio))
        let height = width * ratio + 24
        let nextZ = (notes.map(\.zIndex).max() ?? -1)
        let note = NotesGeometry.constrained(CanvasNote(kind: .image, imageName: name,
            x: Double(point.x), y: Double(point.y), width: width, height: height,
            zIndex: nextZ < Int.max ? nextZ + 1 : nextZ), in: bounds)
        do { try upsert(note) }
        catch { try? fileManager.removeItem(at: url); throw error }
        return note
    }

    private func validImageName(_ name: String) -> Bool {
        guard name.hasSuffix(".png"), name.count == 40 else { return false }
        return UUID(uuidString: String(name.dropLast(4))) != nil
    }

    private func removeImageIfUnused(_ name: String?) {
        guard let name, validImageName(name), !notes.contains(where: { $0.imageName == name }) else { return }
        // Read the committed database as well, so a second store holding an older
        // snapshot cannot unlink an image another note now references.
        let unused = try? withStatement("SELECT COUNT(*) FROM notes WHERE image_name = ?") { statement -> Bool in
            try bind(name, at: 1, to: statement)
            guard sqlite3_step(statement) == SQLITE_ROW else { throw databaseError() }
            return sqlite3_column_int64(statement, 0) == 0
        }
        guard unused == true else { return }
        // Filesystem deletion failure may leave an orphan, but cannot lose a
        // committed note or make a successful database operation appear failed.
        try? fileManager.removeItem(at: imagesDirectory.appendingPathComponent(name))
    }

    private func readNotes(includePayloads: Bool = true) throws -> [CanvasNote] {
        var result: [CanvasNote] = []
        let extras = includePayloads ? ",rich_text,media,drawing" : ""
        try withStatement("SELECT id,kind,text,checklist,image_name,x,y,width,height,z_index,created_at,is_pinned\(extras) FROM notes ORDER BY z_index,created_at,id") { statement in
            while true {
                let status = sqlite3_step(statement)
                if status == SQLITE_DONE { break }
                guard status == SQLITE_ROW else { throw databaseError() }
                guard let idText = string(statement, 0), let id = UUID(uuidString: idText),
                      let rawKind = string(statement, 1), let kind = NoteKind(rawValue: rawKind),
                      let text = string(statement, 2), let checklist = string(statement, 3),
                      let checklistData = checklist.data(using: .utf8),
                      let items = try? JSONDecoder().decode([NoteChecklistItem].self, from: checklistData),
                      Set(items.map(\.id)).count == items.count else { throw NotesStoreError.invalidRecord }
                let imageName = string(statement, 4)
                if let imageName, !validImageName(imageName) { throw NotesStoreError.invalidImageName }
                func payload<T: Decodable>(_ type: T.Type, at index: Int32) throws -> T? {
                    guard includePayloads, sqlite3_column_type(statement, index) != SQLITE_NULL else { return nil }
                    guard let raw = string(statement, index), let data = raw.data(using: .utf8),
                          let value = try? JSONDecoder().decode(type, from: data) else { throw NotesStoreError.invalidRecord }
                    return value
                }
                let rich = try payload(NotesRichText.self, at: 12)
                let media = try payload(NotesMediaReference.self, at: 13)
                let drawing = try payload(NotesDrawing.self, at: 14)
                guard rich.map({ kind == .text && $0.isValid(for: text) }) ?? true,
                      media.map({ kind == .image && $0.isValid }) ?? true,
                      drawing.map({ kind == .text && rich == nil && $0.isValid }) ?? true,
                      kind != .image || imageName != nil || media != nil,
                      (5...10).allSatisfy({ sqlite3_column_type(statement, Int32($0)) == SQLITE_FLOAT || sqlite3_column_type(statement, Int32($0)) == SQLITE_INTEGER }),
                      (5...8).allSatisfy({ sqlite3_column_double(statement, Int32($0)).isFinite }),
                      sqlite3_column_double(statement, 10).isFinite,
                      sqlite3_column_type(statement, 9) == SQLITE_INTEGER,
                      sqlite3_column_type(statement, 11) == SQLITE_INTEGER,
                      [0, 1].contains(sqlite3_column_int(statement, 11)) else { throw NotesStoreError.invalidRecord }
                result.append(NotesGeometry.constrained(CanvasNote(id: id, kind: drawing == nil ? kind : .drawing, text: text, items: items,
                    imageName: imageName, x: sqlite3_column_double(statement, 5), y: sqlite3_column_double(statement, 6),
                    width: sqlite3_column_double(statement, 7), height: sqlite3_column_double(statement, 8),
                    zIndex: Int(sqlite3_column_int64(statement, 9)),
                    createdAt: Date(timeIntervalSinceReferenceDate: sqlite3_column_double(statement, 10)),
                    isPinned: sqlite3_column_int(statement, 11) == 1, richText: rich, media: media, drawing: drawing)))
            }
        }
        return result
    }

    private func sortNotes() {
        notes.sort {
            if $0.zIndex != $1.zIndex { return $0.zIndex < $1.zIndex }
            if $0.createdAt != $1.createdAt { return $0.createdAt < $1.createdAt }
            return $0.id.uuidString < $1.id.uuidString
        }
    }

    private func string(_ statement: OpaquePointer, _ index: Int32) -> String? {
        guard sqlite3_column_type(statement, index) == SQLITE_TEXT,
              let bytes = sqlite3_column_text(statement, index) else { return nil }
        // Explicit length preserves embedded NUL characters in user text.
        let count = Int(sqlite3_column_bytes(statement, index))
        return String(bytes: UnsafeBufferPointer(start: bytes, count: count), encoding: .utf8)
    }

    private func bind(_ value: String?, at index: Int32, to statement: OpaquePointer) throws {
        guard let value else { try checked(sqlite3_bind_null(statement, index)); return }
        let utf8 = Array(value.utf8)
        guard utf8.count < Int(Int32.max) else { throw NotesStoreError.invalidRecord }
        try utf8.withUnsafeBufferPointer { buffer in
            if buffer.isEmpty { try checked(sqlite3_bind_text(statement, index, "", 0, transient)) }
            else {
                try checked(sqlite3_bind_text(statement, index,
                    UnsafeRawPointer(buffer.baseAddress!).assumingMemoryBound(to: CChar.self), Int32(buffer.count), transient))
            }
        }
    }

    private func withStatement<T>(_ sql: String, _ body: (OpaquePointer) throws -> T) throws -> T {
        var statement: OpaquePointer?
        guard sqlite3_prepare_v2(database, sql, -1, &statement, nil) == SQLITE_OK, let statement else {
            if let statement { sqlite3_finalize(statement) }
            throw databaseError()
        }
        defer { sqlite3_finalize(statement) }
        return try body(statement)
    }

    private func execute(_ sql: String) throws {
        guard sqlite3_exec(database, sql, nil, nil, nil) == SQLITE_OK else { throw databaseError() }
    }

    private func transaction(_ operation: () throws -> Void) throws {
        try execute("BEGIN IMMEDIATE")
        do { try operation(); try execute("COMMIT") }
        catch { try? execute("ROLLBACK"); throw error }
    }

    private func checked(_ status: Int32) throws {
        guard status == SQLITE_OK else { throw databaseError() }
    }

    private func databaseError() -> NotesStoreError {
        .database(database.map { String(cString: sqlite3_errmsg($0)) } ?? "The database is closed.")
    }
}
