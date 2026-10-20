import Foundation
import AppKit
import SQLite3

enum ArchiveTemplate: String, Codable, CaseIterable {
    case journal, research
    var title: String { self == .journal ? L10n.text("Journal", "日记") : L10n.text("Q&A", "问答") }
}
enum ArchiveCategoryNameIssue: Equatable { case empty, tooLong, invalidCharacters, reserved, duplicate, unknownCategory }
enum ArchiveCategoryRenameResult: Equatable { case renamed, unchanged, rejected(ArchiveCategoryNameIssue) }
struct ArchiveCategory: Codable, Equatable, Identifiable {
    /// Names typed in the rename field or proposed by Add (spec maxlength 24).
    /// Older names up to 80 characters still load and stay unchanged.
    static let maximumNameLength = 24
    var id = UUID()
    var name: String
    var created = Date()
    /// The NEW badge: set when the category is added, cleared once the user opens it.
    var isNew = false
    private enum CodingKeys: String, CodingKey { case id, name, created }
    var isValid: Bool { !name.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty && name.count <= 80
        && name.utf8.count <= 1024 && !name.contains("\0") && !name.contains("\n") && !name.contains("\r") && created.timeIntervalSince1970.isFinite }
    static func legacy(_ template: ArchiveTemplate) -> ArchiveCategory {
        ArchiveCategory(id: UUID(uuidString: template == .journal ? "8A67BC44-25D4-4DC1-91EF-000000000001" : "8A67BC44-25D4-4DC1-91EF-000000000002")!,
            name: template == .journal ? "Journal" : "Q&A", created: Date(timeIntervalSince1970: 0))
    }
    static func nameKey(_ name: String) -> String { name.trimmingCharacters(in: .whitespacesAndNewlines).folding(options: [.caseInsensitive], locale: Locale(identifier: "en_US_POSIX")) }
    /// The built-in All and Uncategorized filters, in every app language.
    static func isReservedName(_ name: String) -> Bool {
        let key = nameKey(name)
        return AppLanguage.allCases.contains { language in
            nameKey(L10n.text("All", "全部", language: language)) == key || nameKey(L10n.text("Uncategorized", "未分类", language: language)) == key
        }
    }
    /// Validation for a new or renamed category, applied to the trimmed name.
    static func nameIssue(_ name: String, among categories: [ArchiveCategory], excluding id: UUID? = nil) -> ArchiveCategoryNameIssue? {
        let trimmed = name.trimmingCharacters(in: .whitespacesAndNewlines)
        if trimmed.isEmpty { return .empty }
        if trimmed.count > maximumNameLength || trimmed.utf8.count > 1024 { return .tooLong }
        if trimmed.unicodeScalars.contains(where: { [.control, .lineSeparator, .paragraphSeparator].contains($0.properties.generalCategory) }) { return .invalidCharacters }
        if isReservedName(trimmed) { return .reserved }
        let key = nameKey(trimmed)
        if categories.contains(where: { $0.id != id && nameKey($0.name) == key }) { return .duplicate }
        return nil
    }
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
    /// Classified tiles show the stamp and `[ CLASSIFIED ]`, never the title or
    /// media. The controller withholds their thumbnails.
    let isClassified: Bool
    init(id: UUID, template: ArchiveTemplate, title: String, date: Date, mediaCount: Int,
         categoryID: UUID? = nil, thumbnail: NotesMediaReference? = nil, isClassified: Bool = false) {
        self.id = id; self.template = template; self.title = title; self.date = date; self.mediaCount = mediaCount
        self.categoryID = categoryID; self.thumbnail = thumbnail; self.isClassified = isClassified
    }
}
enum ArchiveError: LocalizedError {
    case invalid, unavailable, newerVersion, full, passwordMissing, keychainUnavailable
    var errorDescription: String? {
        switch self {
        case .invalid: return L10n.text("This archive could not be read. The original was preserved.", "无法读取档案，原始数据已保留。")
        case .unavailable: return L10n.text("The archive could not be saved.", "无法保存档案。")
        case .newerVersion: return L10n.text("This archive needs a newer app version.", "此档案需要更新版本的应用。")
        case .full: return L10n.text("The archive is full.", "档案库已满。")
        case .passwordMissing: return L10n.text("This document's password is missing from the Keychain. It stays classified.", "钥匙串中缺少此文档的密码，文档仍保持机密。")
        case .keychainUnavailable: return L10n.text("The Keychain could not be read. The document stays classified.", "无法读取钥匙串，文档仍保持机密。")
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
    /// Stored while no document is classified, so v1.2.0 (which accepts
    /// version 2 and reads only named columns) keeps opening the archive.
    static let sharedSchemaVersion: Int32 = 2
    /// Stored exactly while a document is classified; v1.2.0 refuses it as
    /// newer, so it can never show classified text or clear the flag.
    static let classifiedSchemaVersion: Int32 = 3
    let directory: URL
    /// Password verifiers for classified documents. Never the archive file.
    let vault: ArchivePasswordVault
    private var database: OpaquePointer?
    private let transient = unsafeBitCast(-1, to: sqlite3_destructor_type.self)
    /// Diagnostic processes never touch the Keychain unless a vault is injected.
    init(directory: URL, vault: ArchivePasswordVault? = nil) {
        self.directory = directory
        self.vault = vault ?? (Self.isDiagnostic(CommandLine.arguments) ? ArchiveMemoryPasswordVault() : ArchiveKeychainVault())
    }
    static func applicationDirectory() -> URL {
        directory(notesDirectory: NotesStore.applicationDirectory(), arguments: CommandLine.arguments)
    }
    static func isDiagnostic(_ arguments: [String]) -> Bool {
        arguments.contains { $0 == "--ui-test" || $0.hasSuffix("smoke-test") || $0.hasPrefix("--render-") }
    }
    /// Keep the Notes process's random diagnostic root; never collapse it to /tmp/Archive.
    static func directory(notesDirectory: URL, arguments: [String]) -> URL {
        (isDiagnostic(arguments) ? notesDirectory : notesDirectory.deletingLastPathComponent())
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
            guard value <= Self.classifiedSchemaVersion else { throw ArchiveError.newerVersion }
            guard value >= 0 else { throw ArchiveError.invalid }
            if value == 0 {
                // Only initialize an empty database. Foreign/corrupt schemas are
                // never replaced, repaired or stamped as this app's archive.
                let tables = try statement("SELECT count(*) FROM sqlite_master WHERE name NOT LIKE 'sqlite_%'")
                defer { sqlite3_finalize(tables) }
                guard sqlite3_step(tables) == SQLITE_ROW, sqlite3_column_int(tables, 0) == 0 else { throw ArchiveError.invalid }
                try execute("BEGIN IMMEDIATE")
                do {
                    try execute("CREATE TABLE categories(id TEXT PRIMARY KEY, name TEXT NOT NULL, created REAL NOT NULL, viewed INTEGER NOT NULL DEFAULT 1)")
                    try execute("CREATE TABLE entries(id TEXT PRIMARY KEY, template TEXT NOT NULL, title TEXT NOT NULL, date REAL NOT NULL, modified REAL NOT NULL, mediaCount INTEGER NOT NULL, payload BLOB NOT NULL, categoryID TEXT REFERENCES categories(id), thumbnail BLOB, classification TEXT)")
                    try execute("CREATE TABLE state(key TEXT PRIMARY KEY, value TEXT NOT NULL)")
                    try execute("PRAGMA user_version=\(Self.sharedSchemaVersion)")
                    try execute("COMMIT")
                } catch { try? execute("ROLLBACK"); throw error }
            } else {
                // Validate required columns without any schema writes on reopen.
                do {
                    let entries = try statement("SELECT id,template,title,date,modified,mediaCount,payload FROM entries LIMIT 0")
                    sqlite3_finalize(entries)
                    let state = try statement("SELECT key,value FROM state LIMIT 0")
                    sqlite3_finalize(state)
                    if value >= 2 {
                        let extra = try statement("SELECT categoryID,thumbnail FROM entries LIMIT 0"); sqlite3_finalize(extra)
                        let categories = try statement("SELECT id,name,created FROM categories LIMIT 0"); sqlite3_finalize(categories)
                    }
                    if value >= 3 {
                        let classification = try statement("SELECT classification FROM entries LIMIT 0"); sqlite3_finalize(classification)
                        let viewed = try statement("SELECT viewed FROM categories LIMIT 0"); sqlite3_finalize(viewed)
                    }
                } catch { throw ArchiveError.invalid }
                if value == 1 { try migrateVersionOne() }
                try reconcileSchema()
            }
        } catch { sqlite3_close(db); database = nil; throw error }
    }
    /// Lazily widens a version-two archive with the classification key and
    /// NEW-badge columns, keeping version 2. Both are NULL or defaulted, so
    /// v1.2.0's named-column inserts and whole-row replaces still succeed;
    /// such a replace can only clear a classification, and none exist at
    /// version 2. Also restores "version 3 exactly while anything is
    /// classified" if an earlier build or an outside edit broke it. Writes
    /// nothing when the archive is already consistent.
    private func reconcileSchema() throws {
        func has(_ sql: String) -> Bool { guard let query = try? statement(sql) else { return false }; sqlite3_finalize(query); return true }
        let hasClassification = has("SELECT classification FROM entries LIMIT 0"), hasViewed = has("SELECT viewed FROM categories LIMIT 0")
        if hasClassification && hasViewed, try storedVersion() == classifiedTargetVersion() { return }
        try execute("BEGIN IMMEDIATE")
        do {
            if !hasClassification { try execute("ALTER TABLE entries ADD COLUMN classification TEXT") }
            if !hasViewed { try execute("ALTER TABLE categories ADD COLUMN viewed INTEGER NOT NULL DEFAULT 1") }
            try syncStoredVersion(); try execute("COMMIT")
        } catch { try? execute("ROLLBACK"); throw error }
    }
    private func storedVersion() throws -> Int32 {
        let query = try statement("PRAGMA user_version"); defer { sqlite3_finalize(query) }
        guard sqlite3_step(query) == SQLITE_ROW else { throw ArchiveError.unavailable }
        return sqlite3_column_int(query, 0)
    }
    private func classifiedTargetVersion() throws -> Int32 {
        let query = try statement("SELECT EXISTS(SELECT 1 FROM entries WHERE classification IS NOT NULL)"); defer { sqlite3_finalize(query) }
        guard sqlite3_step(query) == SQLITE_ROW else { throw ArchiveError.unavailable }
        return sqlite3_column_int(query, 0) != 0 ? Self.classifiedSchemaVersion : Self.sharedSchemaVersion
    }
    /// Call inside the transaction that changes a classification, so the
    /// version and the flags commit or roll back together.
    private func syncStoredVersion() throws {
        let target = try classifiedTargetVersion()
        if try storedVersion() != target { try execute("PRAGMA user_version=\(target)") }
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
            for template in templates { try insertCategory(ArchiveCategory.legacy(template), schemaTwo: true) }
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
        try open(); let query = try statement("SELECT id,name,created,viewed FROM categories ORDER BY created,id LIMIT 101")
        defer { sqlite3_finalize(query) }; var result: [ArchiveCategory] = []
        while true {
            let step = sqlite3_step(query); if step == SQLITE_DONE { break }
            guard step == SQLITE_ROW, let id = UUID(uuidString: string(query, 0)), sqlite3_column_type(query, 2) != SQLITE_NULL,
                  sqlite3_column_type(query, 3) == SQLITE_INTEGER, (0...1).contains(sqlite3_column_int64(query, 3)) else { throw ArchiveError.invalid }
            let category = ArchiveCategory(id: id, name: string(query, 1), created: Date(timeIntervalSince1970: sqlite3_column_double(query, 2)),
                isNew: sqlite3_column_int64(query, 3) == 0)
            guard category.isValid, !result.contains(where: { ArchiveCategory.nameKey($0.name) == ArchiveCategory.nameKey(category.name) }) else { throw ArchiveError.invalid }
            result.append(category); guard result.count <= Self.maximumCategories else { throw ArchiveError.full }
        }
        return result
    }
    private func insertCategory(_ value: ArchiveCategory, schemaTwo: Bool = false) throws {
        let query = try statement(schemaTwo ? "INSERT INTO categories(id,name,created) VALUES(?,?,?)" : "INSERT INTO categories(id,name,created,viewed) VALUES(?,?,?,?)")
        defer { sqlite3_finalize(query) }; bind(value.id.uuidString, 1, query); bind(value.name, 2, query)
        sqlite3_bind_double(query, 3, value.created.timeIntervalSince1970)
        if !schemaTwo { sqlite3_bind_int(query, 4, value.isNew ? 0 : 1) }
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
    /// Documents refer to categories by ID, so a rename re-tags all of them at
    /// once. Repeating a completed rename is a no-op.
    func renameCategory(_ id: UUID, to name: String) throws {
        let name = name.trimmingCharacters(in: .whitespacesAndNewlines)
        try open(); try execute("BEGIN IMMEDIATE")
        do {
            let existing = try categories()
            guard let current = existing.first(where: { $0.id == id }) else { throw ArchiveError.invalid }
            if current.name != name {
                guard ArchiveCategory.nameIssue(name, among: existing, excluding: id) == nil else { throw ArchiveError.invalid }
                let query = try statement("UPDATE categories SET name=? WHERE id=?")
                defer { sqlite3_finalize(query) }; bind(name, 1, query); bind(id.uuidString, 2, query)
                guard sqlite3_step(query) == SQLITE_DONE else { throw ArchiveError.unavailable }
            }
            try execute("COMMIT")
        } catch { try? execute("ROLLBACK"); throw error }
    }
    /// Clears the NEW badge. Unknown or removed categories are ignored.
    func markCategoryViewed(_ id: UUID) throws {
        try open(); let query = try statement("UPDATE categories SET viewed=1 WHERE id=?")
        defer { sqlite3_finalize(query) }; bind(id.uuidString, 1, query)
        guard sqlite3_step(query) == SQLITE_DONE else { throw ArchiveError.unavailable }
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
        let query = try statement("SELECT id,template,title,date,mediaCount,categoryID,thumbnail,classification FROM entries ORDER BY modified DESC LIMIT 2001")
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
            let classified = try classificationKey(query, index: 7) != nil
            result.append(ArchiveSummary(id: id, template: template, title: title,
                date: Date(timeIntervalSince1970: timestamp), mediaCount: Int(mediaCount), categoryID: category, thumbnail: thumbnail,
                isClassified: classified))
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
        // An upsert, not a replace: document saves never touch classification,
        // so a stale editor snapshot cannot declassify a document.
        let query = try statement("INSERT INTO entries(id,template,title,date,modified,mediaCount,payload,categoryID,thumbnail) VALUES(?,?,?,?,?,?,?,?,?) "
            + "ON CONFLICT(id) DO UPDATE SET template=excluded.template,title=excluded.title,date=excluded.date,modified=excluded.modified,"
            + "mediaCount=excluded.mediaCount,payload=excluded.payload,categoryID=excluded.categoryID,thumbnail=excluded.thumbnail")
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
    /// A classified document's Keychain verifier is removed after the row.
    func delete(_ id: UUID) throws {
        try open(); try execute("BEGIN IMMEDIATE")
        let key: UUID?
        do {
            // A malformed flag must not block deletion; it only skips cleanup.
            key = (try? classificationRow(id))?.key ?? nil
            let query = try statement("DELETE FROM entries WHERE id=?")
            defer { sqlite3_finalize(query) }; bind(id.uuidString, 1, query)
            guard sqlite3_step(query) == SQLITE_DONE else { throw ArchiveError.unavailable }
            let selection = try statement("DELETE FROM state WHERE key='selection' AND value=?")
            defer { sqlite3_finalize(selection) }; bind(id.uuidString, 1, selection)
            guard sqlite3_step(selection) == SQLITE_DONE else { throw ArchiveError.unavailable }
            if key != nil { try syncStoredVersion() }
            try execute("COMMIT")
        } catch { try? execute("ROLLBACK"); throw error }
        if let key { removeVerifiers(of: id, known: key, keeping: nil) }
    }

    // MARK: Classification
    // The row holds only a random key ID (NULL when unclassified); the salted
    // PBKDF2 verifier lives in the vault under "<document>/<key>". Every new
    // password gets a new key, so an item is never overwritten in place.

    func isClassified(_ id: UUID) throws -> Bool { try open(); return try classificationRow(id).key != nil }
    /// Reads the Keychain only for classified documents; never derives a key.
    func classificationStatus(_ id: UUID) throws -> ArchiveClassificationStatus {
        try open()
        guard let key = try classificationRow(id).key else { return .unclassified }
        do { _ = try storedVerifier(document: id, key: key); return .protected }
        catch ArchiveError.passwordMissing { return .passwordMissing }
        catch { return .keychainUnavailable }
    }
    /// Text is unchanged; only the flag and a new verifier are written.
    func classify(_ id: UUID, password: String) throws {
        guard ArchivePassword.issue(password) == nil else { throw ArchiveError.invalid }
        try open(); try writeVerifier(id, password: password, replacing: false)
    }
    /// True for the right password, and for a document that is not classified.
    /// Throws `.passwordMissing` or `.keychainUnavailable` instead of reporting a wrong password.
    func verify(_ id: UUID, password: String) throws -> Bool {
        try open()
        let row = try classificationRow(id)
        guard row.found else { throw ArchiveError.invalid }
        guard let key = row.key else { return true }
        return try storedVerifier(document: id, key: key).matches(password)
    }
    /// Idempotent. The text stays as it is; the verifier is deleted after commit.
    func declassify(_ id: UUID) throws {
        try open(); try execute("BEGIN IMMEDIATE")
        let key: UUID?
        do {
            key = try classificationRow(id).key
            // Back to version 2 with the last classified document, so v1.2.0
            // can open the archive again; nothing it could misread remains.
            if key != nil { try setClassification(id, key: nil); try syncStoredVersion() }
            try execute("COMMIT")
        } catch { try? execute("ROLLBACK"); throw error }
        if let key { removeVerifiers(of: id, known: key, keeping: nil) }
    }
    /// The recovery path for a classified document without a readable
    /// verifier: a new password keeps it classified; nil declassifies it.
    /// Refused while the current verifier is readable. Callers must first
    /// authenticate the Mac's owner (the controller does).
    func recoverClassification(_ id: UUID, newPassword: String?) throws {
        if let newPassword { guard ArchivePassword.issue(newPassword) == nil else { throw ArchiveError.invalid } }
        let status = try classificationStatus(id)
        guard status == .passwordMissing || status == .keychainUnavailable else { throw ArchiveError.invalid }
        if let newPassword { try writeVerifier(id, password: newPassword, replacing: true) } else { try declassify(id) }
    }
    private func writeVerifier(_ id: UUID, password: String, replacing: Bool) throws {
        // Derive before taking the write lock; this is the slow step.
        let data = try ArchivePasswordVerifier.make(password: password).encoded()
        let key = UUID(), account = ArchivePassword.account(document: id, key: key)
        try execute("BEGIN IMMEDIATE")
        var added = false, previous: UUID?
        do {
            let row = try classificationRow(id); previous = row.key
            guard row.found, (row.key != nil) == replacing else { throw ArchiveError.invalid }
            do { try vault.add(data, account: account) } catch { throw ArchiveError.keychainUnavailable }
            added = true
            try setClassification(id, key: key)
            try syncStoredVersion() // The first classified document locks v1.2.0 out.
            try execute("COMMIT")
        } catch {
            try? execute("ROLLBACK")
            if added { try? vault.remove(account: account) }
            throw error
        }
        removeVerifiers(of: id, known: previous, keeping: key)
    }
    private func storedVerifier(document id: UUID, key: UUID) throws -> ArchivePasswordVerifier {
        let data: Data?
        do { data = try vault.read(account: ArchivePassword.account(document: id, key: key)) } catch { throw ArchiveError.keychainUnavailable }
        guard let data, let verifier = ArchivePasswordVerifier(data: data) else { throw ArchiveError.passwordMissing }
        return verifier
    }
    /// Best effort after a commit: a leftover verifier is harmless and never
    /// matched again, because the row names its own key.
    private func removeVerifiers(of id: UUID, known: UUID?, keeping: UUID?) {
        if let known, known != keeping { try? vault.remove(account: ArchivePassword.account(document: id, key: known)) }
        let prefix = id.uuidString + "/", kept = keeping.map { ArchivePassword.account(document: id, key: $0) }
        for account in (try? vault.accounts()) ?? [] where account.hasPrefix(prefix) && account != kept {
            try? vault.remove(account: account)
        }
    }
    private func classificationRow(_ id: UUID) throws -> (found: Bool, key: UUID?) {
        let query = try statement("SELECT classification FROM entries WHERE id=?")
        defer { sqlite3_finalize(query) }; bind(id.uuidString, 1, query)
        let step = sqlite3_step(query); if step == SQLITE_DONE { return (false, nil) }
        guard step == SQLITE_ROW else { throw ArchiveError.unavailable }
        return (true, try classificationKey(query, index: 0))
    }
    private func classificationKey(_ query: OpaquePointer, index: Int32) throws -> UUID? {
        if sqlite3_column_type(query, index) == SQLITE_NULL { return nil }
        guard sqlite3_column_type(query, index) == SQLITE_TEXT, let key = UUID(uuidString: string(query, index)) else { throw ArchiveError.invalid }
        return key
    }
    private func setClassification(_ id: UUID, key: UUID?) throws {
        let query = try statement("UPDATE entries SET classification=? WHERE id=?")
        defer { sqlite3_finalize(query) }
        if let key { bind(key.uuidString, 1, query) } else { sqlite3_bind_null(query, 1) }
        bind(id.uuidString, 2, query)
        guard sqlite3_step(query) == SQLITE_DONE, sqlite3_changes(database) == 1 else { throw ArchiveError.unavailable }
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
/// A classified document opens locked. A correct password unlocks it for one
/// visit; leaving it, creating or deleting a document, or hiding the Archive
/// (module switch or HUD close) locks it again. Locked documents reject edits.
final class ArchiveController {
    typealias Executor = (@escaping () -> Void) -> Void
    private struct Pending { let entry: ArchiveEntry; let revision: UInt64 }
    private struct Drain { let completion: (Bool) -> Void; let timeout: Timer }
    let store: ArchiveStore
    private let work: Executor
    private let deliver: Executor
    private let ownerAuthentication: ArchiveOwnerAuthenticating
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
    // Renames and badge clears of committed categories. While a category's own
    // creation is queued, they are folded into that draft instead.
    private var categoryRenames: [UUID: String] = [:]
    private var renamingCategories: Set<UUID> = []
    private var viewedCategories: Set<UUID> = []
    private var markingViewed: Set<UUID> = []
    private var classified: Set<UUID> = []
    private var verifierProblems: [UUID: ArchiveClassificationStatus] = [:]
    private var classificationWork: Set<UUID> = []
    private var unlocking: UUID?
    private var unlockedID: UUID?
    private var visit: UInt64 = 0
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
    /// The open document. While `isSelectedLocked`, its text and media are
    /// still present for sizing the redaction bars, but must not be shown.
    private(set) var selected: ArchiveEntry?
    private(set) var error: String?
    var isBusy: Bool { operations > 0 }
    var hasUnsavedChanges: Bool {
        !dirty.isEmpty || !dirtyCategories.isEmpty || !categoryRenames.isEmpty || !categoryRemovals.isEmpty || !deleting.isEmpty
    }
    var onChange: (() -> Void)?
    var onEvent: ((String) -> Void)?

    /// Injection keeps race/failure tests deterministic and off the user's store.
    /// A supplied worker must preserve FIFO order, like the production queue.
    init(store: ArchiveStore, work: Executor? = nil, deliver: Executor? = nil, ownerAuthentication: ArchiveOwnerAuthenticating? = nil) {
        self.store = store
        let queue = DispatchQueue(label: "EndfieldHUD.Archive", qos: .utility)
        self.work = work ?? { queue.async(execute: $0) }
        // AppKit can enter its termination/modal run loop from a main GCD
        // callback. That loop cannot re-enter the same dispatch queue.
        self.deliver = deliver ?? { operation in
            RunLoop.main.perform(inModes: [.common, .modalPanel], block: operation)
        }
        self.ownerAuthentication = ownerAuthentication ?? ArchiveDeviceOwnerAuthentication()
    }
    func activate() {
        active = true
        invalidatePresentation(); let token = generation
        perform({ [store] in
            let entries = try store.summaries(), id = try store.selection(), entry = try id.flatMap(store.entry)
            return (entries, entry, id, try store.categories(), try entry.map { try store.classificationStatus($0.id) })
        }) { [self] result in
            guard generation == token else { return }
            switch result {
            case .success(let value):
                classified = Set(value.0.filter(\.isClassified).map(\.id))
                categories = value.3.filter { categoryRemovals[$0.id] == nil }
                for category in dirtyCategories.values { upsertCategory(category) }
                applyPendingCategoryEdits()
                entries = boundedSummaries(value.0.filter { !deleting.contains($0.id) }.map(applyingCategoryRemovals))
                for pending in dirty.values { upsertSummary(pending.entry) }
                selected = value.2.flatMap { deleting.contains($0) ? nil : dirty[$0]?.entry ?? value.1.map(applyingCategoryRemovals) }
                if let id = value.1?.id, let status = value.4 { record(status, for: id) }
                readError = nil
            case .failure(let failure): readError = failure.localizedDescription
            }
            changed()
        }
    }
    func deactivate() { active = false; relock(); flush(); invalidatePresentation(); selected = nil }
    func select(_ id: UUID?) {
        guard id.map({ !deleting.contains($0) }) ?? true else { return }
        if id == nil || id != unlockedID { relock() }
        flush(); invalidatePresentation(); let token = generation
        let outgoing = selected
        if id == nil { selected = nil }
        changed()
        perform({ [store] in
            let entry = try id.flatMap(store.entry); try store.select(id)
            return (entry, try entry.map { try store.classificationStatus($0.id) })
        }) { [self] result in
            guard generation == token else { return }
            switch result {
            case .success(let value):
                if let entry = value.0, let status = value.1 { record(status, for: entry.id) }
                selected = id.flatMap { dirty[$0]?.entry ?? value.0.map(applyingCategoryRemovals) }; readError = nil
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
        relock(); flush(); invalidatePresentation()
        let entry = ArchiveEntry(template: template, categoryID: categoryID)
        unannouncedCreations.insert(entry.id)
        selected = entry; update(entry); flush(); changed()
        perform({ [store] in try store.select(entry.id) }) { [self] result in
            if case .failure(let failure) = result { readError = failure.localizedDescription; changed() }
        }
    }
    /// Returns an existing category of the same normalized name instead of a
    /// duplicate. A new category carries the NEW badge.
    @discardableResult func createCategory(named name: String) -> UUID? {
        let value = ArchiveCategory(name: name.trimmingCharacters(in: .whitespacesAndNewlines), isNew: true)
        guard value.isValid else { readError = ArchiveError.invalid.localizedDescription; changed(); return nil }
        if let existing = categories.first(where: { ArchiveCategory.nameKey($0.name) == ArchiveCategory.nameKey(value.name) }) { return existing.id }
        return insertDraftCategory(value)
    }
    private func insertDraftCategory(_ value: ArchiveCategory) -> UUID? {
        guard categories.count + categoryRemovals.count < ArchiveStore.maximumCategories else { readError = ArchiveError.full.localizedDescription; changed(); return nil }
        guard writeErrors.isEmpty && categoryErrors.isEmpty else { changed(); return nil }
        dirtyCategories[value.id] = value; upsertCategory(value); flush(); changed(); return value.id
    }
    func moveSelected(to categoryID: UUID?) {
        guard var entry = selected, !isLocked(entry.id), entry.categoryID != categoryID,
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
        categoryErrors.removeValue(forKey: id); categoryRenames.removeValue(forKey: id); viewedCategories.remove(id)
        for documentID in affected {
            if var entry = dirty[documentID]?.entry ?? (selected?.id == documentID ? selected : nil) {
                entry.categoryID = nil; revision &+= 1; dirty[documentID] = Pending(entry: entry, revision: revision)
                if selected?.id == documentID { selected = entry }
            }
        }
        entries = entries.map(applyingCategoryRemovals)
        flush(); changed()
    }

    // MARK: Editable categories
    // All and Uncategorized are filters (nil), not rows, so no call can rename
    // or remove them; their names are reserved in every language.

    /// The `+` action: appends "Category N" with the NEW badge.
    @discardableResult func addCategory() -> UUID? { insertDraftCategory(ArchiveCategory(name: nextCategoryName(), isNew: true)) }
    /// N starts at the number of custom categories plus one and skips names in use.
    func nextCategoryName() -> String {
        var number = categories.count + 1
        while true {
            let name = L10n.text("Category \(number)", "分类 \(number)")
            if ArchiveCategory.nameIssue(name, among: categories) == nil || number > 2 * ArchiveStore.maximumCategories { return name }
            number += 1
        }
    }
    /// Live validation for the rename field; nil when the name can be committed.
    func categoryNameIssue(_ name: String, renaming id: UUID? = nil) -> ArchiveCategoryNameIssue? {
        ArchiveCategory.nameIssue(name, among: categories, excluding: id)
    }
    /// Commits a trimmed, valid, unique name at once; storage follows in order.
    /// Documents refer to the category's ID, so they all follow the new name.
    @discardableResult func renameCategory(_ id: UUID, to proposed: String) -> ArchiveCategoryRenameResult {
        guard let index = categories.firstIndex(where: { $0.id == id }), categoryRemovals[id] == nil else { return .rejected(.unknownCategory) }
        let name = proposed.trimmingCharacters(in: .whitespacesAndNewlines)
        if name == categories[index].name { return .unchanged }
        if let issue = ArchiveCategory.nameIssue(name, among: categories, excluding: id) { return .rejected(issue) }
        var value = categories[index]; value.name = name; upsertCategory(value)
        if dirtyCategories[id] != nil && !savingCategories.contains(id) { dirtyCategories[id]?.name = name }
        else { categoryRenames[id] = name }
        flush(); changed(); return .renamed
    }
    /// Clears the NEW badge; call when the user opens the category. A failed
    /// write is not retried: at worst the badge returns after a relaunch.
    func markCategoryViewed(_ id: UUID) {
        guard let index = categories.firstIndex(where: { $0.id == id }), categories[index].isNew else { return }
        categories[index].isNew = false
        if dirtyCategories[id] != nil && !savingCategories.contains(id) { dirtyCategories[id]?.isNew = false }
        else { viewedCategories.insert(id) }
        flush(); changed()
    }
    private func applyPendingCategoryEdits() {
        for (index, category) in categories.enumerated() {
            if let name = categoryRenames[category.id] { categories[index].name = name }
            if viewedCategories.contains(category.id) { categories[index].isNew = false }
        }
    }

    // MARK: Classification

    func isClassified(_ id: UUID) -> Bool { classified.contains(id) }
    /// The open document's state, including a missing or unreadable verifier
    /// (checked when the document is opened and on every unlock attempt).
    var selectedClassification: ArchiveClassificationStatus {
        guard let id = selected?.id, classified.contains(id) else { return .unclassified }
        return verifierProblems[id] ?? .protected
    }
    /// The locked view replaces the editor while this is true.
    var isSelectedLocked: Bool { selected.map { isLocked($0.id) } ?? false }
    /// A classify, unlock, declassify or recovery request is running.
    var isClassificationBusy: Bool { unlocking != nil || !classificationWork.isEmpty }
    private func isLocked(_ id: UUID) -> Bool { classified.contains(id) && unlockedID != id }
    private func relock() { unlockedID = nil; visit &+= 1 }

    /// Saves the latest title and body first, then sets the flag and stores
    /// the verifier. On success the document is locked (the sealing view).
    func classifySelected(password: String, completion: ((Result<Void, Error>) -> Void)? = nil) {
        guard let id = selected?.id, !deleting.contains(id), !classified.contains(id), !classificationWork.contains(id),
              ArchivePassword.issue(password) == nil else { completion?(.failure(ArchiveError.invalid)); return }
        flush() // FIFO: the pending document save commits before the flag.
        classificationWork.insert(id); changed()
        perform({ [store] in try store.classify(id, password: password) }) { [self] result in
            classificationWork.remove(id)
            if case .success = result {
                classified.insert(id); verifierProblems.removeValue(forKey: id)
                if unlockedID == id { relock() }
                refreshSummary(id); onEvent?("classified")
            }
            changed(); completion?(result)
        }
    }
    /// Verifies on the worker (about 85 ms). A missing or unreadable Keychain
    /// item is reported as such, never as a wrong password.
    func unlockSelected(password: String, completion: @escaping (ArchiveUnlockResult) -> Void) {
        guard let id = selected?.id else { completion(.ignored); return }
        guard isLocked(id) else { completion(.unlocked); return }
        guard unlocking == nil, !classificationWork.contains(id) else { completion(.ignored); return }
        let token = visit; unlocking = id; changed()
        perform({ [store] in try store.verify(id, password: password) }) { [self] result in
            unlocking = nil
            let outcome: ArchiveUnlockResult
            switch result {
            case .success(let matches):
                if matches { verifierProblems.removeValue(forKey: id) }
                outcome = !matches ? .wrongPassword : visit == token && selected?.id == id ? .unlocked : .ignored
                if outcome == .unlocked { unlockedID = id }
            case .failure(let failure):
                let problem = failure as? ArchiveError
                if problem == .passwordMissing { verifierProblems[id] = .passwordMissing }
                if problem == .keychainUnavailable { verifierProblems[id] = .keychainUnavailable }
                outcome = problem == .passwordMissing ? .passwordMissing : .failed(failure.localizedDescription)
            }
            changed(); completion(outcome)
        }
    }
    /// Back and Esc relock through `select(nil)`; this relocks in place.
    func lockSelected() { relock(); flush(); changed() }
    /// Only while unlocked in this visit. The text stays; the verifier is deleted.
    func declassifySelected(completion: ((Result<Void, Error>) -> Void)? = nil) {
        guard let id = selected?.id, classified.contains(id), unlockedID == id, !classificationWork.contains(id) else {
            completion?(.failure(ArchiveError.invalid)); return
        }
        classificationWork.insert(id); changed()
        perform({ [store] in try store.declassify(id) }) { [self] result in
            classificationWork.remove(id)
            if case .success = result { markDeclassified(id) }
            changed(); completion?(result)
        }
    }
    /// Re-reads the Keychain state of the open document, e.g. after the user
    /// unlocks the login keychain.
    func refreshSelectedClassification() {
        guard let id = selected?.id, classified.contains(id) else { return }
        perform({ [store] in try store.classificationStatus(id) }) { [self] result in
            if case .success(let status) = result { record(status, for: id) }
            changed()
        }
    }
    /// The recovery path for `.passwordMissing` and `.keychainUnavailable`.
    /// macOS first verifies the Mac's owner (Touch ID or login password); then a
    /// new password keeps the document classified and unlocks this visit, or
    /// nil declassifies it. The store refuses while a readable verifier exists.
    func recoverSelectedClassification(newPassword: String?, completion: @escaping (Result<Void, Error>) -> Void) {
        guard let id = selected?.id, classified.contains(id), verifierProblems[id] != nil,
              !classificationWork.contains(id), unlocking == nil,
              newPassword.map({ ArchivePassword.issue($0) == nil }) ?? true else { completion(.failure(ArchiveError.invalid)); return }
        let token = visit; classificationWork.insert(id); changed()
        let reason = L10n.text("restore access to a classified document", "恢复对机密文档的访问")
        ownerAuthentication.authenticateOwner(reason: reason) { [weak self] approved in
            self?.deliver { [weak self] in self?.finishRecovery(id, newPassword: newPassword, approved: approved, visit: token, completion: completion) }
        }
    }
    private func finishRecovery(_ id: UUID, newPassword: String?, approved: Bool, visit token: UInt64, completion: @escaping (Result<Void, Error>) -> Void) {
        guard approved, visit == token, selected?.id == id else {
            classificationWork.remove(id); changed(); completion(.failure(CocoaError(.userCancelled))); return
        }
        perform({ [store] in try store.recoverClassification(id, newPassword: newPassword) }) { [self] result in
            classificationWork.remove(id)
            switch result {
            case .success:
                if newPassword == nil { markDeclassified(id) }
                else {
                    verifierProblems.removeValue(forKey: id); onEvent?("classificationRecovered")
                    if visit == token && selected?.id == id { unlockedID = id }
                }
            case .failure: refreshSelectedClassification()
            }
            changed(); completion(result)
        }
    }
    private func markDeclassified(_ id: UUID) {
        classified.remove(id); verifierProblems.removeValue(forKey: id)
        if unlockedID == id { unlockedID = nil }
        refreshSummary(id); onEvent?("declassified")
    }
    private func record(_ status: ArchiveClassificationStatus, for id: UUID) {
        if status == .unclassified { classified.remove(id) } else { classified.insert(id) }
        if status == .passwordMissing || status == .keychainUnavailable { verifierProblems[id] = status }
        else { verifierProblems.removeValue(forKey: id) }
        refreshSummary(id)
    }

    private func applyingCategoryRemovals(_ entry: ArchiveEntry) -> ArchiveEntry {
        var value = entry
        if let category = value.categoryID, categoryRemovals[category] != nil { value.categoryID = nil }
        return value
    }
    private func applyingCategoryRemovals(_ summary: ArchiveSummary) -> ArchiveSummary {
        guard let category = summary.categoryID, categoryRemovals[category] != nil else { return summary }
        return ArchiveSummary(id: summary.id, template: summary.template, title: summary.title, date: summary.date,
            mediaCount: summary.mediaCount, categoryID: nil, thumbnail: summary.thumbnail, isClassified: summary.isClassified)
    }
    /// Visible thumbnail caches can recover descriptors omitted by the aggregate
    /// summary budget without decoding document bodies or opening media files.
    /// Classified documents never yield a thumbnail.
    func loadThumbnail(for id: UUID, completion: @escaping (NotesMediaReference?) -> Void) {
        guard active, !classified.contains(id) else { completion(nil); return }
        if let pending = dirty[id] { completion(pending.entry.media.first); return }
        if thumbnailRequests[id] != nil {
            guard thumbnailRequests[id]!.callbacks.count < 8 else { completion(nil); return }
            thumbnailRequests[id]!.callbacks.append(completion); return
        }
        guard thumbnailRequests.count < 32 else { completion(nil); return }
        let token = UUID(); thumbnailRequests[id] = ThumbnailRequest(token: token, callbacks: [completion])
        perform({ [store] in try store.thumbnail(for: id) }) { [self] result in
            guard thumbnailRequests[id]?.token == token, let request = thumbnailRequests.removeValue(forKey: id) else { return }
            let value = active && !classified.contains(id) ? (try? result.get()) : nil
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
        guard entry.isValid, selected?.id == entry.id, !deleting.contains(entry.id), !isLocked(entry.id),
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
        // Renames go first: they only target committed categories, and may free
        // a name that a queued creation below then uses.
        for (id, name) in categoryRenames where !renamingCategories.contains(id) && dirtyCategories[id] == nil && categoryRemovals[id] == nil {
            renamingCategories.insert(id)
            perform({ [store] in try store.renameCategory(id, to: name) }) { [self] result in
                renamingCategories.remove(id)
                guard categoryRemovals[id] == nil else { changed(); return }
                switch result {
                case .success:
                    if categoryRenames[id] == name { categoryRenames.removeValue(forKey: id) }
                    categoryErrors.removeValue(forKey: id); onEvent?("categoryRenamed")
                    if categoryRenames[id] != nil { flush() }
                case .failure(let failure): categoryErrors[id] = failure.localizedDescription
                }
                changed()
            }
        }
        // Queue categories before documents that refer to them, including retry.
        for (id, category) in dirtyCategories where !savingCategories.contains(id) && categoryRemovals[id] == nil {
            savingCategories.insert(id)
            perform({ [store] in try store.saveCategory(category) }) { [self] result in
                savingCategories.remove(id)
                switch result {
                case .success:
                    dirtyCategories.removeValue(forKey: id); categoryErrors.removeValue(forKey: id); onEvent?("categoryCreated")
                    if categoryRenames[id] != nil || viewedCategories.contains(id) { flush() }
                case .failure(let failure):
                    if categoryRemovals[id] == nil { categoryErrors[id] = failure.localizedDescription }
                    // The retry inserts the newest name and badge state directly.
                    if let name = categoryRenames.removeValue(forKey: id) { dirtyCategories[id]?.name = name }
                    if viewedCategories.remove(id) != nil { dirtyCategories[id]?.isNew = false }
                }
                changed()
            }
        }
        for id in viewedCategories where !markingViewed.contains(id) && dirtyCategories[id] == nil && categoryRemovals[id] == nil {
            markingViewed.insert(id)
            perform({ [store] in try store.markCategoryViewed(id) }) { [self] _ in markingViewed.remove(id); viewedCategories.remove(id) }
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
    /// Locked documents cannot be deleted; unlock (or recover) first.
    func deleteSelected() {
        guard let entry = selected, !deleting.contains(entry.id), !isLocked(entry.id) else { return }
        let id = entry.id, unsaved = dirty.removeValue(forKey: entry.id)
        saveWork?.cancel(); saveWork = nil
        deleting.insert(id); relock(); invalidatePresentation(); selected = nil
        if let removed = entries.first(where: { $0.id == id }) { summaryThumbnailBytes -= thumbnailCost(removed.thumbnail) }
        entries.removeAll { $0.id == id }; changed()
        // FIFO guarantees any already queued save completes before DELETE.
        // Clearing selection and tombstoning now rejects late editor callbacks.
        perform({ [store] in try store.delete(id) }) { [self] result in
            deleting.remove(id)
            switch result {
            case .success:
                writeErrors.removeValue(forKey: id); unannouncedCreations.remove(id); categoryChanges.removeValue(forKey: id)
                classified.remove(id); verifierProblems.removeValue(forKey: id); onEvent?("deleted")
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
            let isClassified = classified.contains(value.id), candidate = isClassified ? nil : value.thumbnail
            let cost = thumbnailCost(candidate)
            let thumbnail = cost <= ArchiveStore.maximumSummaryThumbnailBytes - summaryThumbnailBytes ? candidate : nil
            summaryThumbnailBytes += thumbnailCost(thumbnail)
            return ArchiveSummary(id: value.id, template: value.template, title: value.title, date: value.date,
                mediaCount: value.mediaCount, categoryID: value.categoryID, thumbnail: thumbnail, isClassified: isClassified)
        }
    }
    private func upsertSummary(_ entry: ArchiveEntry) {
        if let existing = entries.first(where: { $0.id == entry.id }) { summaryThumbnailBytes -= thumbnailCost(existing.thumbnail) }
        entries.removeAll { $0.id == entry.id }
        let isClassified = classified.contains(entry.id), candidate = isClassified ? nil : entry.media.first
        let thumbnail = thumbnailCost(candidate) <= ArchiveStore.maximumSummaryThumbnailBytes - summaryThumbnailBytes ? candidate : nil
        summaryThumbnailBytes += thumbnailCost(thumbnail)
        entries.insert(ArchiveSummary(id: entry.id, template: entry.template, title: entry.title,
            date: entry.date, mediaCount: entry.media.count, categoryID: entry.categoryID, thumbnail: thumbnail, isClassified: isClassified), at: 0)
    }
    /// Re-derives one tile's classified flag and thumbnail in place.
    private func refreshSummary(_ id: UUID) {
        guard let index = entries.firstIndex(where: { $0.id == id }) else { return }
        let old = entries[index], isClassified = classified.contains(id)
        summaryThumbnailBytes -= thumbnailCost(old.thumbnail)
        var thumbnail = isClassified ? nil : old.thumbnail ?? (selected?.id == id ? selected?.media.first : nil)
        if thumbnailCost(thumbnail) > ArchiveStore.maximumSummaryThumbnailBytes - summaryThumbnailBytes { thumbnail = nil }
        summaryThumbnailBytes += thumbnailCost(thumbnail)
        entries[index] = ArchiveSummary(id: old.id, template: old.template, title: old.title, date: old.date,
            mediaCount: old.mediaCount, categoryID: old.categoryID, thumbnail: thumbnail, isClassified: isClassified)
    }
    private func changed() { error = writeErrors.values.first ?? categoryErrors.values.first ?? readError; onChange?() }
}
