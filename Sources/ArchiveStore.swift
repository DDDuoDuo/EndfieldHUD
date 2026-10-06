import Foundation
import AppKit
import SQLite3

enum ArchiveTemplate: String, Codable, CaseIterable {
    case journal, research
    var title: String { self == .journal ? L10n.text("Journal", "日记") : L10n.text("Q&A", "问答") }
}
struct ArchiveCategory: Codable, Equatable, Identifiable {
    var id = UUID()
    var name: String
    var created = Date()
    var isValid: Bool { !name.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty && name.count <= 80
        && name.utf8.count <= 1024 && !name.contains("\0") && !name.contains("\n") && !name.contains("\r") && created.timeIntervalSince1970.isFinite }
    static func legacy(_ template: ArchiveTemplate) -> ArchiveCategory {
        ArchiveCategory(id: UUID(uuidString: template == .journal ? "8A67BC44-25D4-4DC1-91EF-000000000001" : "8A67BC44-25D4-4DC1-91EF-000000000002")!,
            name: template == .journal ? "Journal" : "Q&A", created: Date(timeIntervalSince1970: 0))
    }
    static func nameKey(_ name: String) -> String { name.trimmingCharacters(in: .whitespacesAndNewlines).folding(options: [.caseInsensitive], locale: Locale(identifier: "en_US_POSIX")) }
}
struct ArchiveEntry: Codable, Equatable, Identifiable {
    var id = UUID()
    var template: ArchiveTemplate
    var title = ""
    var date = Date()
    var body = ""
    var media: [NotesMediaReference] = []
    var modified = Date()
    var categoryID: UUID? = nil
    var titleRichText: NotesRichText? = nil
    var bodyRichText: NotesRichText? = nil
    var titleStyle = NotesTextStyle(fontSize: 17)
    var bodyStyle = NotesTextStyle()
    private enum CodingKeys: String, CodingKey { case id, template, title, date, body, media, modified, categoryID, titleRichText, bodyRichText, titleStyle, bodyStyle }
    var isValid: Bool {
        title.count <= 200 && !title.contains("\0") && body.utf8.count <= ArchiveStore.maximumBodyBytes && media.count <= 16
            && media.allSatisfy(\.isValid) && date.timeIntervalSince1970.isFinite && modified.timeIntervalSince1970.isFinite
            && titleStyle.isValid && bodyStyle.isValid
            && (titleRichText?.isValid(for: title) ?? true) && (bodyRichText?.isValid(for: body) ?? true)
    }
}
extension ArchiveEntry {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        id = try c.decode(UUID.self, forKey: .id); template = try c.decode(ArchiveTemplate.self, forKey: .template)
        title = try c.decode(String.self, forKey: .title); date = try c.decode(Date.self, forKey: .date)
        body = try c.decode(String.self, forKey: .body); media = try c.decode([NotesMediaReference].self, forKey: .media)
        modified = try c.decode(Date.self, forKey: .modified)
        categoryID = try c.decodeIfPresent(UUID.self, forKey: .categoryID)
        titleRichText = try c.decodeIfPresent(NotesRichText.self, forKey: .titleRichText)
        bodyRichText = try c.decodeIfPresent(NotesRichText.self, forKey: .bodyRichText)
        titleStyle = try c.decodeIfPresent(NotesTextStyle.self, forKey: .titleStyle) ?? NotesTextStyle(fontSize: 17)
        bodyStyle = try c.decodeIfPresent(NotesTextStyle.self, forKey: .bodyStyle) ?? NotesTextStyle()
    }
}
struct ArchiveSummary: Equatable, Identifiable {
    let id: UUID
    let template: ArchiveTemplate
    let title: String
    let date: Date
    let mediaCount: Int
    let categoryID: UUID?
    let thumbnail: NotesMediaReference?
    init(id: UUID, template: ArchiveTemplate, title: String, date: Date, mediaCount: Int,
         categoryID: UUID? = nil, thumbnail: NotesMediaReference? = nil) {
        self.id = id; self.template = template; self.title = title; self.date = date; self.mediaCount = mediaCount
        self.categoryID = categoryID; self.thumbnail = thumbnail
    }
}
enum ArchiveError: LocalizedError {
    case invalid, unavailable, newerVersion, full
    var errorDescription: String? {
        switch self {
        case .invalid: return L10n.text("This archive could not be read. The original was preserved.", "无法读取档案，原始数据已保留。")
        case .unavailable: return L10n.text("The archive could not be saved.", "无法保存档案。")
        case .newerVersion: return L10n.text("This archive needs a newer app version.", "此档案需要更新版本的应用。")
        case .full: return L10n.text("The archive is full.", "档案库已满。")
        }
    }
}

/// A separate, lazily opened SQLite document store. The gallery selects only
/// small metadata rows; only the selected document's text/media is decoded.
/// One serial controller queue owns it. No source media is copied or deleted.
final class ArchiveStore {
    static let maximumBodyBytes = 2 * 1024 * 1024
    static let maximumEntries = 2000
    static let maximumCategories = 100
    static let maximumSummaryThumbnailBytes = 16 * 1024 * 1024
    let directory: URL
    private var database: OpaquePointer?
    private let transient = unsafeBitCast(-1, to: sqlite3_destructor_type.self)
    init(directory: URL) { self.directory = directory }
    static func applicationDirectory() -> URL {
        directory(notesDirectory: NotesStore.applicationDirectory(), arguments: CommandLine.arguments)
    }
    /// Keep the Notes process's random diagnostic root; never collapse it to /tmp/Archive.
    static func directory(notesDirectory: URL, arguments: [String]) -> URL {
        let diagnostic = arguments.contains {
            $0 == "--ui-test" || $0.hasSuffix("smoke-test") || $0.hasPrefix("--render-")
        }
        return (diagnostic ? notesDirectory : notesDirectory.deletingLastPathComponent())
            .appendingPathComponent("Archive", isDirectory: true)
    }
    deinit { if let database { sqlite3_close(database) } }
    private func open() throws {
        guard database == nil else { return }
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        var db: OpaquePointer?
        guard sqlite3_open_v2(directory.appendingPathComponent("archive.sqlite").path, &db,
                SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nil) == SQLITE_OK, let db else {
            if let db { sqlite3_close(db) }; throw ArchiveError.unavailable
        }
        database = db
        do {
            sqlite3_busy_timeout(db, 1200)
            let version = try statement("PRAGMA user_version")
            defer { sqlite3_finalize(version) }
            guard sqlite3_step(version) == SQLITE_ROW else { throw ArchiveError.invalid }
            let value = sqlite3_column_int(version, 0)
            guard value <= 2 else { throw ArchiveError.newerVersion }
            guard value >= 0 else { throw ArchiveError.invalid }
            if value == 0 {
                // Only initialize an empty database. Foreign/corrupt schemas are
                // never replaced, repaired or stamped as this app's archive.
                let tables = try statement("SELECT count(*) FROM sqlite_master WHERE name NOT LIKE 'sqlite_%'")
                defer { sqlite3_finalize(tables) }
                guard sqlite3_step(tables) == SQLITE_ROW, sqlite3_column_int(tables, 0) == 0 else { throw ArchiveError.invalid }
                try execute("BEGIN IMMEDIATE")
                do {
                    try execute("CREATE TABLE categories(id TEXT PRIMARY KEY, name TEXT NOT NULL, created REAL NOT NULL)")
                    try execute("CREATE TABLE entries(id TEXT PRIMARY KEY, template TEXT NOT NULL, title TEXT NOT NULL, date REAL NOT NULL, modified REAL NOT NULL, mediaCount INTEGER NOT NULL, payload BLOB NOT NULL, categoryID TEXT REFERENCES categories(id), thumbnail BLOB)")
                    try execute("CREATE TABLE state(key TEXT PRIMARY KEY, value TEXT NOT NULL)")
                    try execute("PRAGMA user_version=2")
                    try execute("COMMIT")
                } catch { try? execute("ROLLBACK"); throw error }
            } else {
                // Validate required columns without any schema writes on reopen.
                do {
                    let entries = try statement("SELECT id,template,title,date,modified,mediaCount,payload FROM entries LIMIT 0")
                    sqlite3_finalize(entries)
                    let state = try statement("SELECT key,value FROM state LIMIT 0")
                    sqlite3_finalize(state)
                    if value == 2 {
                        let extra = try statement("SELECT categoryID,thumbnail FROM entries LIMIT 0"); sqlite3_finalize(extra)
                        let categories = try statement("SELECT id,name,created FROM categories LIMIT 0"); sqlite3_finalize(categories)
                    }
                } catch { throw ArchiveError.invalid }
                if value == 1 { try migrateVersionOne() }
            }
        } catch { sqlite3_close(db); database = nil; throw error }
    }
    /// Validate every original record before schema mutation, then backfill only
    /// small metadata. Original payload bytes, document IDs and selection survive.
    private func migrateVersionOne() throws {
        try execute("BEGIN IMMEDIATE")
        do {
            var templates = Set<ArchiveTemplate>()
            let validate = try statement("SELECT payload,id,template,title,date,modified,mediaCount FROM entries")
            do {
                var count = 0
                while true {
                    let step = sqlite3_step(validate); if step == SQLITE_DONE { break }
                    guard step == SQLITE_ROW else { throw ArchiveError.invalid }
                    try autoreleasepool {
                        guard let id = UUID(uuidString: string(validate, 1)) else { throw ArchiveError.invalid }
                        let entry = try decodePayload(validate, index: 0)
                        guard entry.id == id, entry.template.rawValue == string(validate, 2), entry.title == string(validate, 3),
                              abs(entry.date.timeIntervalSince1970 - sqlite3_column_double(validate, 4)) < 0.000_01,
                              abs(entry.modified.timeIntervalSince1970 - sqlite3_column_double(validate, 5)) < 0.000_01,
                              entry.media.count == sqlite3_column_int64(validate, 6), entry.categoryID == nil else { throw ArchiveError.invalid }
                        templates.insert(entry.template)
                    }
                    count += 1; guard count <= Self.maximumEntries else { throw ArchiveError.full }
                }
                sqlite3_finalize(validate)
            } catch { sqlite3_finalize(validate); throw error }
            _ = try selection()
            try execute("CREATE TABLE categories(id TEXT PRIMARY KEY, name TEXT NOT NULL, created REAL NOT NULL)")
            try execute("ALTER TABLE entries ADD COLUMN categoryID TEXT REFERENCES categories(id)")
            try execute("ALTER TABLE entries ADD COLUMN thumbnail BLOB")
            for template in templates { try insertCategory(ArchiveCategory.legacy(template)) }
            let rows = try statement("SELECT id,payload FROM entries")
            do {
                while true {
                    let step = sqlite3_step(rows); if step == SQLITE_DONE { break }
                    guard step == SQLITE_ROW else { throw ArchiveError.invalid }
                    try autoreleasepool {
                        let entry = try decodePayload(rows, index: 1)
                        let update = try statement("UPDATE entries SET categoryID=?,thumbnail=? WHERE id=?")
                        defer { sqlite3_finalize(update) }
                        bind(ArchiveCategory.legacy(entry.template).id.uuidString, 1, update)
                        try bindThumbnail(entry.media.first, 2, update); bind(entry.id.uuidString, 3, update)
                        guard sqlite3_step(update) == SQLITE_DONE else { throw ArchiveError.unavailable }
                    }
                }
                sqlite3_finalize(rows)
            } catch { sqlite3_finalize(rows); throw error }
            try execute("PRAGMA user_version=2"); try execute("COMMIT")
        } catch { try? execute("ROLLBACK"); throw error }
    }
    private func decodePayload(_ query: OpaquePointer, index: Int32) throws -> ArchiveEntry {
        let count = sqlite3_column_bytes(query, index)
        guard count > 0, count <= 20 * 1024 * 1024, let bytes = sqlite3_column_blob(query, index),
              let value = try? JSONDecoder().decode(ArchiveEntry.self, from: Data(bytes: bytes, count: Int(count))), value.isValid else { throw ArchiveError.invalid }
        return value
    }
    private func categoryID(_ query: OpaquePointer, index: Int32) throws -> UUID? {
        if sqlite3_column_type(query, index) == SQLITE_NULL { return nil }
        guard let id = UUID(uuidString: string(query, index)) else { throw ArchiveError.invalid }; return id
    }
    private func decodeThumbnail(_ query: OpaquePointer, index: Int32) throws -> NotesMediaReference? {
        if sqlite3_column_type(query, index) == SQLITE_NULL { return nil }
        let count = sqlite3_column_bytes(query, index)
        guard count > 0, count <= 2 * 1024 * 1024, let bytes = sqlite3_column_blob(query, index),
              let value = try? JSONDecoder().decode(NotesMediaReference.self, from: Data(bytes: bytes, count: Int(count))), value.isValid else { throw ArchiveError.invalid }
        return value
    }
    private func bindThumbnail(_ value: NotesMediaReference?, _ index: Int32, _ query: OpaquePointer) throws {
        if let value { let data = try JSONEncoder().encode(value); _ = data.withUnsafeBytes { sqlite3_bind_blob(query, index, $0.baseAddress, Int32(data.count), transient) } }
        else { sqlite3_bind_null(query, index) }
    }
    func categories() throws -> [ArchiveCategory] {
        try open(); let query = try statement("SELECT id,name,created FROM categories ORDER BY created,id LIMIT 101")
        defer { sqlite3_finalize(query) }; var result: [ArchiveCategory] = []
        while true {
            let step = sqlite3_step(query); if step == SQLITE_DONE { break }
            guard step == SQLITE_ROW, let id = UUID(uuidString: string(query, 0)), sqlite3_column_type(query, 2) != SQLITE_NULL else { throw ArchiveError.invalid }
            let category = ArchiveCategory(id: id, name: string(query, 1), created: Date(timeIntervalSince1970: sqlite3_column_double(query, 2)))
            guard category.isValid, !result.contains(where: { ArchiveCategory.nameKey($0.name) == ArchiveCategory.nameKey(category.name) }) else { throw ArchiveError.invalid }
            result.append(category); guard result.count <= Self.maximumCategories else { throw ArchiveError.full }
        }
        return result
    }
    private func insertCategory(_ value: ArchiveCategory) throws {
        let query = try statement("INSERT INTO categories(id,name,created) VALUES(?,?,?)")
        defer { sqlite3_finalize(query) }; bind(value.id.uuidString, 1, query); bind(value.name, 2, query)
        sqlite3_bind_double(query, 3, value.created.timeIntervalSince1970)
        guard sqlite3_step(query) == SQLITE_DONE else { throw ArchiveError.unavailable }
    }
    func saveCategory(_ value: ArchiveCategory) throws {
        guard value.isValid else { throw ArchiveError.invalid }; try open(); try execute("BEGIN IMMEDIATE")
        do {
            let existing = try categories()
            if let same = existing.first(where: { $0.id == value.id }) {
                guard same.name == value.name, abs(same.created.timeIntervalSince1970 - value.created.timeIntervalSince1970) < 0.000_01 else { throw ArchiveError.invalid }
            }
            else {
                guard existing.count < Self.maximumCategories else { throw ArchiveError.full }
                guard !existing.contains(where: { ArchiveCategory.nameKey($0.name) == ArchiveCategory.nameKey(value.name) }) else { throw ArchiveError.invalid }
                try insertCategory(value)
            }
            try execute("COMMIT")
        } catch { try? execute("ROLLBACK"); throw error }
    }
    func thumbnail(for id: UUID) throws -> NotesMediaReference? {
        try open(); let query = try statement("SELECT thumbnail FROM entries WHERE id=?")
        defer { sqlite3_finalize(query) }; bind(id.uuidString, 1, query)
        let step = sqlite3_step(query); if step == SQLITE_DONE { return nil }
        guard step == SQLITE_ROW else { throw ArchiveError.invalid }; return try decodeThumbnail(query, index: 0)
    }
    private func statement(_ sql: String) throws -> OpaquePointer {
        var item: OpaquePointer?
        guard sqlite3_prepare_v2(database, sql, -1, &item, nil) == SQLITE_OK, let item else { throw ArchiveError.unavailable }
        return item
    }
    private func execute(_ sql: String) throws {
        guard sqlite3_exec(database, sql, nil, nil, nil) == SQLITE_OK else { throw ArchiveError.unavailable }
    }
    private func bind(_ value: String, _ column: Int32, _ query: OpaquePointer) {
        _ = value.withCString { sqlite3_bind_text(query, column, $0, Int32(value.utf8.count), transient) }
    }
    private func string(_ query: OpaquePointer, _ column: Int32) -> String {
        guard let bytes = sqlite3_column_text(query, column) else { return "" }
        return String(decoding: UnsafeBufferPointer(start: bytes, count: Int(sqlite3_column_bytes(query, column))), as: UTF8.self)
    }
    func summaries() throws -> [ArchiveSummary] {
        try open()
        let categories = Set(try self.categories().map(\.id))
        let query = try statement("SELECT id,template,title,date,mediaCount,categoryID,thumbnail FROM entries ORDER BY modified DESC LIMIT 2001")
        defer { sqlite3_finalize(query) }
        var result: [ArchiveSummary] = [], thumbnailBytes = 0
        while true {
            let step = sqlite3_step(query)
            if step == SQLITE_DONE { break }
            guard step == SQLITE_ROW, let id = UUID(uuidString: string(query, 0)),
                  let template = ArchiveTemplate(rawValue: string(query, 1)) else { throw ArchiveError.invalid }
            let title = string(query, 2), timestamp = sqlite3_column_double(query, 3)
            let mediaCount = sqlite3_column_int64(query, 4)
            guard title.count <= 200, !title.contains("\0"), timestamp.isFinite,
                  sqlite3_column_type(query, 3) != SQLITE_NULL, (0...16).contains(mediaCount) else { throw ArchiveError.invalid }
            let category = try categoryID(query, index: 5)
            guard category.map(categories.contains) ?? true else { throw ArchiveError.invalid }
            var thumbnail = try decodeThumbnail(query, index: 6)
            guard (mediaCount == 0) == (thumbnail == nil) else { throw ArchiveError.invalid }
            let bytes = Int(sqlite3_column_bytes(query, 6))
            if bytes > Self.maximumSummaryThumbnailBytes - thumbnailBytes { thumbnail = nil }
            else { thumbnailBytes += bytes }
            result.append(ArchiveSummary(id: id, template: template, title: title,
                date: Date(timeIntervalSince1970: timestamp), mediaCount: Int(mediaCount), categoryID: category, thumbnail: thumbnail))
            guard result.count <= Self.maximumEntries else { throw ArchiveError.full }
        }
        return result
    }
    func entry(_ id: UUID) throws -> ArchiveEntry? {
        try open(); let query = try statement("SELECT payload,template,title,date,modified,mediaCount,categoryID,thumbnail FROM entries WHERE id=?")
        defer { sqlite3_finalize(query) }; bind(id.uuidString, 1, query)
        let step = sqlite3_step(query); if step == SQLITE_DONE { return nil }
        guard step == SQLITE_ROW else { throw ArchiveError.invalid }
        var value = try decodePayload(query, index: 0)
        let category = try categoryID(query, index: 6), thumbnail = try decodeThumbnail(query, index: 7)
        guard value.id == id, value.template.rawValue == string(query, 1), value.title == string(query, 2),
              abs(value.date.timeIntervalSince1970 - sqlite3_column_double(query, 3)) < 0.000_01,
              abs(value.modified.timeIntervalSince1970 - sqlite3_column_double(query, 4)) < 0.000_01,
              value.media.count == sqlite3_column_int64(query, 5), value.media.first == thumbnail else { throw ArchiveError.invalid }
        // v1 payloads are deliberately not rewritten by migration. The only
        // permitted additive mismatch is their deterministic legacy category.
        if value.categoryID != category {
            guard value.categoryID == nil, category == ArchiveCategory.legacy(value.template).id else { throw ArchiveError.invalid }
        }
        if let category { guard try categories().contains(where: { $0.id == category }) else { throw ArchiveError.invalid } }
        value.categoryID = category; return value
    }
    private func writeEntry(_ entry: ArchiveEntry) throws {
        guard entry.isValid else { throw ArchiveError.invalid }
        let payload = try JSONEncoder().encode(entry)
        guard payload.count <= 20 * 1024 * 1024 else { throw ArchiveError.invalid }
        if let category = entry.categoryID {
            guard try categories().contains(where: { $0.id == category }) else { throw ArchiveError.invalid }
        }
        let count = try statement("SELECT COUNT(*) FROM entries WHERE id!=?")
        bind(entry.id.uuidString, 1, count)
        let step = sqlite3_step(count), total = sqlite3_column_int(count, 0)
        sqlite3_finalize(count)
        guard step == SQLITE_ROW else { throw ArchiveError.unavailable }
        guard total < Self.maximumEntries else { throw ArchiveError.full }
        let query = try statement("INSERT OR REPLACE INTO entries(id,template,title,date,modified,mediaCount,payload,categoryID,thumbnail) VALUES(?,?,?,?,?,?,?,?,?)")
        defer { sqlite3_finalize(query) }
        bind(entry.id.uuidString, 1, query); bind(entry.template.rawValue, 2, query); bind(entry.title, 3, query)
        sqlite3_bind_double(query, 4, entry.date.timeIntervalSince1970)
        sqlite3_bind_double(query, 5, entry.modified.timeIntervalSince1970)
        sqlite3_bind_int(query, 6, Int32(entry.media.count))
        _ = payload.withUnsafeBytes { sqlite3_bind_blob(query, 7, $0.baseAddress, Int32(payload.count), transient) }
        if let category = entry.categoryID { bind(category.uuidString, 8, query) } else { sqlite3_bind_null(query, 8) }
        try bindThumbnail(entry.media.first, 9, query)
        guard sqlite3_step(query) == SQLITE_DONE else { throw ArchiveError.unavailable }
    }
    func save(_ entry: ArchiveEntry) throws {
        guard entry.isValid else { throw ArchiveError.invalid }; try open(); try execute("BEGIN IMMEDIATE")
        do { try writeEntry(entry); try execute("COMMIT") }
        catch { try? execute("ROLLBACK"); throw error }
    }
    /// Reassign every document and remove only the category, in one transaction.
    /// Latest dirty drafts join that transaction; original files and selection
    /// are untouched. Decode one persisted payload at a time to bound memory.
    func deleteCategory(_ id: UUID, preserving drafts: [ArchiveEntry] = []) throws {
        guard drafts.count <= Self.maximumEntries, Set(drafts.map(\.id)).count == drafts.count,
              drafts.allSatisfy({ $0.isValid && $0.categoryID != id }) else { throw ArchiveError.invalid }
        try open(); try execute("BEGIN IMMEDIATE")
        do {
            let query = try statement("SELECT id FROM entries WHERE categoryID=? LIMIT 2001")
            bind(id.uuidString, 1, query)
            var ids: [UUID] = []
            do {
                while true {
                    let step = sqlite3_step(query); if step == SQLITE_DONE { break }
                    guard step == SQLITE_ROW, let value = UUID(uuidString: string(query, 0)) else { throw ArchiveError.invalid }
                    ids.append(value); guard ids.count <= Self.maximumEntries else { throw ArchiveError.full }
                }
                sqlite3_finalize(query)
            } catch { sqlite3_finalize(query); throw error }
            let draftIDs = Set(drafts.map(\.id))
            for value in ids where !draftIDs.contains(value) {
                try autoreleasepool {
                    guard var entry = try self.entry(value) else { throw ArchiveError.invalid }
                    entry.categoryID = nil; try writeEntry(entry)
                }
            }
            for draft in drafts { try writeEntry(draft) }
            let removal = try statement("DELETE FROM categories WHERE id=?")
            defer { sqlite3_finalize(removal) }; bind(id.uuidString, 1, removal)
            guard sqlite3_step(removal) == SQLITE_DONE else { throw ArchiveError.unavailable }
            try execute("COMMIT")
        } catch { try? execute("ROLLBACK"); throw error }
    }
    func delete(_ id: UUID) throws {
        try open(); try execute("BEGIN IMMEDIATE")
        do {
            let query = try statement("DELETE FROM entries WHERE id=?")
            defer { sqlite3_finalize(query) }; bind(id.uuidString, 1, query)
            guard sqlite3_step(query) == SQLITE_DONE else { throw ArchiveError.unavailable }
            let selection = try statement("DELETE FROM state WHERE key='selection' AND value=?")
            defer { sqlite3_finalize(selection) }; bind(id.uuidString, 1, selection)
            guard sqlite3_step(selection) == SQLITE_DONE else { throw ArchiveError.unavailable }
            try execute("COMMIT")
        } catch { try? execute("ROLLBACK"); throw error }
    }
    func selection() throws -> UUID? {
        try open(); let query = try statement("SELECT value FROM state WHERE key='selection'")
        defer { sqlite3_finalize(query) }
        let step = sqlite3_step(query)
        if step == SQLITE_DONE { return nil }
        guard step == SQLITE_ROW else { throw ArchiveError.unavailable }
        let value = string(query, 0)
        if value.isEmpty { return nil }
        guard let id = UUID(uuidString: value) else { throw ArchiveError.invalid }
        return id
    }
    func select(_ id: UUID?) throws {
        try open(); let query = try statement("INSERT OR REPLACE INTO state VALUES('selection',?)")
        defer { sqlite3_finalize(query) }; bind(id?.uuidString ?? "", 1, query)
        guard sqlite3_step(query) == SQLITE_DONE else { throw ArchiveError.unavailable }
    }
}

/// Main-thread state with one serial utility worker. Dirty snapshots remain
/// owned until SQLite acknowledges their exact revision, including while hidden.
/// Only edits schedule a finite debounce; failed writes never spin in a retry loop.
final class ArchiveController {
    typealias Executor = (@escaping () -> Void) -> Void
    private struct Pending { let entry: ArchiveEntry; let revision: UInt64 }
    private struct Drain { let completion: (Bool) -> Void; let timeout: Timer }
    let store: ArchiveStore
    private let work: Executor
    private let deliver: Executor
    private var saveWork: DispatchWorkItem?
    private var dirty: [UUID: Pending] = [:]
    private var saving: [UUID: UInt64] = [:]
    private var deleting: Set<UUID> = []
    private var unannouncedCreations: Set<UUID> = []
    private var dirtyCategories: [UUID: ArchiveCategory] = [:]
    private var savingCategories: Set<UUID> = []
    private var categoryErrors: [UUID: String] = [:]
    private var categoryChanges: [UUID: UInt64] = [:]
    private var categoryRemovals: [UUID: Set<UUID>] = [:]
    private var removingCategories: Set<UUID> = []
    private struct ThumbnailRequest { let token: UUID; var callbacks: [(NotesMediaReference?) -> Void] }
    private var thumbnailRequests: [UUID: ThumbnailRequest] = [:]
    private var summaryThumbnailBytes = 0
    private var active = false
    private var writeErrors: [UUID: String] = [:]
    private var readError: String?
    private var drains: [UUID: Drain] = [:]
    private var revision: UInt64 = 0
    private var operations = 0
    private var generation = 0
    private(set) var entries: [ArchiveSummary] = []
    private(set) var categories: [ArchiveCategory] = []
    private(set) var selected: ArchiveEntry?
    private(set) var error: String?
    var isBusy: Bool { operations > 0 }
    var hasUnsavedChanges: Bool { !dirty.isEmpty || !dirtyCategories.isEmpty || !categoryRemovals.isEmpty || !deleting.isEmpty }
    var onChange: (() -> Void)?
    var onEvent: ((String) -> Void)?

    /// Injection keeps race/failure tests deterministic and off the user's store.
    /// A supplied worker must preserve FIFO order, like the production queue.
    init(store: ArchiveStore, work: Executor? = nil, deliver: Executor? = nil) {
        self.store = store
        let queue = DispatchQueue(label: "EndfieldHUD.Archive", qos: .utility)
        self.work = work ?? { queue.async(execute: $0) }
        // AppKit can enter its termination/modal run loop from a main GCD
        // callback. That loop cannot re-enter the same dispatch queue.
        self.deliver = deliver ?? { operation in
            RunLoop.main.perform(inModes: [.common, .modalPanel], block: operation)
        }
    }
    func activate() {
        active = true
        invalidatePresentation(); let token = generation
        perform({ [store] in
            let entries = try store.summaries(), id = try store.selection()
            return (entries, try id.flatMap(store.entry), id, try store.categories())
        }) { [self] result in
            guard generation == token else { return }
            switch result {
            case .success(let value):
                categories = value.3.filter { categoryRemovals[$0.id] == nil }
                for category in dirtyCategories.values { upsertCategory(category) }
                entries = boundedSummaries(value.0.filter { !deleting.contains($0.id) }.map(applyingCategoryRemovals))
                for pending in dirty.values { upsertSummary(pending.entry) }
                selected = value.2.flatMap { deleting.contains($0) ? nil : dirty[$0]?.entry ?? value.1.map(applyingCategoryRemovals) }
                readError = nil
            case .failure(let failure): readError = failure.localizedDescription
            }
            changed()
        }
    }
    func deactivate() { active = false; flush(); invalidatePresentation(); selected = nil }
    func select(_ id: UUID?) {
        guard id.map({ !deleting.contains($0) }) ?? true else { return }
        flush(); invalidatePresentation(); let token = generation
        let outgoing = selected
        if id == nil { selected = nil }
        changed()
        perform({ [store] in
            let entry = try id.flatMap(store.entry); try store.select(id); return entry
        }) { [self] result in
            guard generation == token else { return }
            switch result {
            case .success(let entry): selected = id.flatMap { dirty[$0]?.entry ?? entry.map(applyingCategoryRemovals) }; readError = nil
            case .failure(let failure):
                // Even a disk failure must not discard the editable in-memory draft.
                let retained = outgoing.flatMap { prior in selected?.id == prior.id ? selected : dirty[prior.id]?.entry ?? prior }
                selected = id.flatMap { dirty[$0]?.entry ?? retained }; readError = failure.localizedDescription
            }
            changed()
        }
    }
    func create(_ template: ArchiveTemplate) { create(template: template, categoryID: nil) }
    func create(categoryID: UUID?) { create(template: .journal, categoryID: categoryID) }
    private func create(template: ArchiveTemplate, categoryID: UUID?) {
        guard categoryID.map({ id in categories.contains { $0.id == id } }) ?? true else { readError = ArchiveError.invalid.localizedDescription; changed(); return }
        guard entries.count < ArchiveStore.maximumEntries else { readError = ArchiveError.full.localizedDescription; changed(); return }
        // Do not accumulate unbounded full-size drafts while storage is failing.
        guard writeErrors.isEmpty && categoryErrors.isEmpty else { changed(); return }
        flush(); invalidatePresentation()
        let entry = ArchiveEntry(template: template, categoryID: categoryID)
        unannouncedCreations.insert(entry.id)
        selected = entry; update(entry); flush(); changed()
        perform({ [store] in try store.select(entry.id) }) { [self] result in
            if case .failure(let failure) = result { readError = failure.localizedDescription; changed() }
        }
    }
    @discardableResult func createCategory(named name: String) -> UUID? {
        let value = ArchiveCategory(name: name.trimmingCharacters(in: .whitespacesAndNewlines))
        guard value.isValid else { readError = ArchiveError.invalid.localizedDescription; changed(); return nil }
        if let existing = categories.first(where: { ArchiveCategory.nameKey($0.name) == ArchiveCategory.nameKey(value.name) }) { return existing.id }
        guard categories.count + categoryRemovals.count < ArchiveStore.maximumCategories else { readError = ArchiveError.full.localizedDescription; changed(); return nil }
        guard writeErrors.isEmpty && categoryErrors.isEmpty else { changed(); return nil }
        dirtyCategories[value.id] = value; upsertCategory(value); flush(); changed(); return value.id
    }
    func moveSelected(to categoryID: UUID?) {
        guard var entry = selected, entry.categoryID != categoryID,
              categoryID.map({ id in categories.contains { $0.id == id } }) ?? true else { return }
        entry.categoryID = categoryID; update(entry); categoryChanges[entry.id] = revision; flush(); changed()
    }
    /// Confirmed deletion removes the category only. Pending references become
    /// uncategorized immediately and remain owned until the transaction commits.
    func deleteCategory(_ id: UUID) {
        guard categories.contains(where: { $0.id == id }), categoryRemovals[id] == nil else { return }
        var affected = Set(entries.filter { $0.categoryID == id }.map(\.id))
        for pending in dirty.values where pending.entry.categoryID == id { affected.insert(pending.entry.id) }
        if let entry = selected, entry.categoryID == id { affected.insert(entry.id) }
        categoryRemovals[id] = affected
        categories.removeAll { $0.id == id }; dirtyCategories.removeValue(forKey: id)
        categoryErrors.removeValue(forKey: id)
        for documentID in affected {
            if var entry = dirty[documentID]?.entry ?? (selected?.id == documentID ? selected : nil) {
                entry.categoryID = nil; revision &+= 1; dirty[documentID] = Pending(entry: entry, revision: revision)
                if selected?.id == documentID { selected = entry }
            }
        }
        entries = entries.map(applyingCategoryRemovals)
        flush(); changed()
    }
    private func applyingCategoryRemovals(_ entry: ArchiveEntry) -> ArchiveEntry {
        var value = entry
        if let category = value.categoryID, categoryRemovals[category] != nil { value.categoryID = nil }
        return value
    }
    private func applyingCategoryRemovals(_ summary: ArchiveSummary) -> ArchiveSummary {
        guard let category = summary.categoryID, categoryRemovals[category] != nil else { return summary }
        return ArchiveSummary(id: summary.id, template: summary.template, title: summary.title, date: summary.date,
            mediaCount: summary.mediaCount, categoryID: nil, thumbnail: summary.thumbnail)
    }
    /// Visible thumbnail caches can recover descriptors omitted by the aggregate
    /// summary budget without decoding document bodies or opening media files.
    func loadThumbnail(for id: UUID, completion: @escaping (NotesMediaReference?) -> Void) {
        guard active else { completion(nil); return }
        if let pending = dirty[id] { completion(pending.entry.media.first); return }
        if thumbnailRequests[id] != nil {
            guard thumbnailRequests[id]!.callbacks.count < 8 else { completion(nil); return }
            thumbnailRequests[id]!.callbacks.append(completion); return
        }
        guard thumbnailRequests.count < 32 else { completion(nil); return }
        let token = UUID(); thumbnailRequests[id] = ThumbnailRequest(token: token, callbacks: [completion])
        perform({ [store] in try store.thumbnail(for: id) }) { [self] result in
            guard thumbnailRequests[id]?.token == token, let request = thumbnailRequests.removeValue(forKey: id) else { return }
            let value = active ? (try? result.get()) : nil
            request.callbacks.forEach { $0(value) }
        }
    }
    private func invalidatePresentation() {
        generation += 1
        let requests = Array(thumbnailRequests.values); thumbnailRequests.removeAll()
        for request in requests { request.callbacks.forEach { $0(nil) } }
    }
    private func upsertCategory(_ category: ArchiveCategory) {
        categories.removeAll { $0.id == category.id }; categories.append(category)
        categories.sort { $0.created == $1.created ? $0.id.uuidString < $1.id.uuidString : $0.created < $1.created }
    }
    func update(_ entry: ArchiveEntry) {
        guard entry.isValid, selected?.id == entry.id, !deleting.contains(entry.id),
              entry.categoryID.map({ id in categories.contains { $0.id == id } }) ?? true else { return }
        var entry = entry; entry.modified = Date(); selected = entry
        revision &+= 1; dirty[entry.id] = Pending(entry: entry, revision: revision)
        upsertSummary(entry)
        saveWork?.cancel()
        let task = DispatchWorkItem { [weak self] in self?.flush() }; saveWork = task
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.35, execute: task)
    }
    func flush() {
        saveWork?.cancel(); saveWork = nil
        // Queue categories before documents that refer to them, including retry.
        for (id, category) in dirtyCategories where !savingCategories.contains(id) && categoryRemovals[id] == nil {
            savingCategories.insert(id)
            perform({ [store] in try store.saveCategory(category) }) { [self] result in
                savingCategories.remove(id)
                switch result {
                case .success: dirtyCategories.removeValue(forKey: id); categoryErrors.removeValue(forKey: id); onEvent?("categoryCreated")
                case .failure(let failure): if categoryRemovals[id] == nil { categoryErrors[id] = failure.localizedDescription }
                }
                changed()
            }
        }
        // Existing queued writes precede removal. Newer affected drafts are
        // written by this transaction, never independently before its outcome.
        let blockedDocuments = Set(categoryRemovals.values.flatMap { $0 })
        for (id, documents) in categoryRemovals where !removingCategories.contains(id) {
            removingCategories.insert(id)
            let included = dirty.filter { documents.contains($0.key) && !deleting.contains($0.key) }
            perform({ [store] in try store.deleteCategory(id, preserving: included.values.map(\.entry)) }) { [self] result in
                removingCategories.remove(id)
                switch result {
                case .success:
                    categoryRemovals.removeValue(forKey: id); categoryErrors.removeValue(forKey: id)
                    for (documentID, pending) in included where !deleting.contains(documentID) {
                        writeErrors.removeValue(forKey: documentID)
                        if dirty[documentID]?.revision == pending.revision { dirty.removeValue(forKey: documentID) }
                        if unannouncedCreations.remove(documentID) != nil { onEvent?("created") }
                        if let moved = categoryChanges[documentID], moved <= pending.revision { categoryChanges.removeValue(forKey: documentID) }
                    }
                    onEvent?("categoryDeleted")
                    if documents.contains(where: { dirty[$0] != nil && !deleting.contains($0) }) { flush() }
                case .failure(let failure): categoryErrors[id] = failure.localizedDescription
                }
                changed()
            }
        }
        for (id, pending) in dirty where saving[id] == nil && !deleting.contains(id) && !blockedDocuments.contains(id) {
            saving[id] = pending.revision
            perform({ [store] in try store.save(pending.entry) }) { [self] result in
                saving.removeValue(forKey: id)
                guard !deleting.contains(id) else { return }
                switch result {
                case .success:
                    writeErrors.removeValue(forKey: id)
                    if dirty[id]?.revision == pending.revision { dirty.removeValue(forKey: id) }
                    if unannouncedCreations.remove(id) != nil { onEvent?("created") }
                    if let categoryRevision = categoryChanges[id], pending.revision >= categoryRevision {
                        categoryChanges.removeValue(forKey: id); onEvent?("categoryChanged")
                    }
                case .failure(let failure): writeErrors[id] = failure.localizedDescription
                }
                // A newer edit that arrived during I/O is a new request. Retry
                // only that revision, never repeatedly retry the failed one.
                if let next = dirty[id], next.revision != pending.revision { flush() }
                changed()
            }
        }
    }
    func retryPendingSaves() { readError = nil; flush(); changed() }
    func deleteSelected() {
        guard let entry = selected, !deleting.contains(entry.id) else { return }
        let id = entry.id, unsaved = dirty.removeValue(forKey: entry.id)
        saveWork?.cancel(); saveWork = nil
        deleting.insert(id); invalidatePresentation(); selected = nil
        if let removed = entries.first(where: { $0.id == id }) { summaryThumbnailBytes -= thumbnailCost(removed.thumbnail) }
        entries.removeAll { $0.id == id }; changed()
        // FIFO guarantees any already queued save completes before DELETE.
        // Clearing selection and tombstoning now rejects late editor callbacks.
        perform({ [store] in try store.delete(id) }) { [self] result in
            deleting.remove(id)
            switch result {
            case .success:
                writeErrors.removeValue(forKey: id); unannouncedCreations.remove(id); categoryChanges.removeValue(forKey: id); onEvent?("deleted")
            case .failure(let failure):
                if let unsaved { dirty[id] = unsaved }
                upsertSummary(entry); writeErrors[id] = failure.localizedDescription
            }
            changed()
        }
    }
    /// Finite shutdown barrier. Failure returns false with dirty drafts retained;
    /// timeout stops waiting but does not cancel or misreport an in-flight save.
    func drainPendingWrites(timeout: TimeInterval = 3, completion: @escaping (Bool) -> Void) {
        flush()
        guard isBusy else { completion(!hasUnsavedChanges && writeErrors.isEmpty && categoryErrors.isEmpty); return }
        let id = UUID()
        let seconds = max(0, min(10, timeout.isFinite ? timeout : 3))
        let deadline = Timer(timeInterval: seconds, repeats: false) { [weak self] _ in
            guard let drain = self?.drains.removeValue(forKey: id) else { return }; drain.completion(false)
        }
        drains[id] = Drain(completion: completion, timeout: deadline)
        RunLoop.main.add(deadline, forMode: .common)
        RunLoop.main.add(deadline, forMode: .modalPanel)
    }
    private func perform<T>(_ action: @escaping () throws -> T, completion: @escaping (Result<T, Error>) -> Void) {
        operations += 1
        work { [self] in
            let result = Result { try action() }
            deliver { [self] in
                operations -= 1; completion(result)
                guard !isBusy else { return }
                let waiting = Array(drains.values); drains.removeAll()
                for drain in waiting { drain.timeout.invalidate(); drain.completion(!hasUnsavedChanges && writeErrors.isEmpty && categoryErrors.isEmpty) }
            }
        }
    }
    // Conservative O(1) descriptor cost; never re-encode bookmarks on keystrokes.
    private func thumbnailCost(_ value: NotesMediaReference?) -> Int {
        guard let value else { return 0 }
        return value.bookmark.count * 2 + (value.lastKnownPath.utf8.count + value.displayName.utf8.count) * 6 + 512
    }
    private func boundedSummaries(_ values: [ArchiveSummary]) -> [ArchiveSummary] {
        summaryThumbnailBytes = 0
        return values.map { value in
            let cost = thumbnailCost(value.thumbnail)
            let thumbnail = cost <= ArchiveStore.maximumSummaryThumbnailBytes - summaryThumbnailBytes ? value.thumbnail : nil
            summaryThumbnailBytes += thumbnailCost(thumbnail)
            return ArchiveSummary(id: value.id, template: value.template, title: value.title, date: value.date,
                mediaCount: value.mediaCount, categoryID: value.categoryID, thumbnail: thumbnail)
        }
    }
    private func upsertSummary(_ entry: ArchiveEntry) {
        if let existing = entries.first(where: { $0.id == entry.id }) { summaryThumbnailBytes -= thumbnailCost(existing.thumbnail) }
        entries.removeAll { $0.id == entry.id }
        let candidate = entry.media.first
        let thumbnail = thumbnailCost(candidate) <= ArchiveStore.maximumSummaryThumbnailBytes - summaryThumbnailBytes ? candidate : nil
        summaryThumbnailBytes += thumbnailCost(thumbnail)
        entries.insert(ArchiveSummary(id: entry.id, template: entry.template, title: entry.title,
            date: entry.date, mediaCount: entry.media.count, categoryID: entry.categoryID, thumbnail: thumbnail), at: 0)
    }
    private func changed() { error = writeErrors.values.first ?? categoryErrors.values.first ?? readError; onChange?() }
}
