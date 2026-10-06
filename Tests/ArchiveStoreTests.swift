import Foundation
import SQLite3

enum ArchiveStoreTests {
    private final class Scheduler {
        var work: [() -> Void] = [], delivery: [() -> Void] = []
        var performed = 0
        func execute() { precondition(!work.isEmpty); performed += 1; work.removeFirst()() }
        func deliver() { precondition(!delivery.isEmpty); delivery.removeFirst()() }
        func drain() {
            var bound = 0
            while !work.isEmpty || !delivery.isEmpty {
                bound += 1; precondition(bound < 300, "Archive work must converge without retry loops")
                if !work.isEmpty { execute() }
                if !delivery.isEmpty { deliver() }
            }
        }
        func controller(_ store: ArchiveStore) -> ArchiveController {
            ArchiveController(store: store, work: { self.work.append($0) }, deliver: { self.delivery.append($0) })
        }
    }
    static func run() -> Int {
        var count = 0
        func check(_ condition: Bool, _ message: String, file: StaticString = #file, line: UInt = #line) {
            count += 1; if !condition { fatalError(message, file: file, line: line) }
        }
        func rejects(_ action: () throws -> Void) -> Bool { do { try action(); return false } catch { return true } }
        let fm = FileManager.default
        let root = fm.temporaryDirectory.appendingPathComponent("ArchiveStoreTests-\(UUID())", isDirectory: true)
        defer { try? fm.removeItem(at: root) }
        func sql(_ directory: URL, _ command: String) throws {
            try fm.createDirectory(at: directory, withIntermediateDirectories: true)
            var db: OpaquePointer?
            guard sqlite3_open(directory.appendingPathComponent("archive.sqlite").path, &db) == SQLITE_OK else { throw ArchiveError.unavailable }
            defer { sqlite3_close(db) }
            guard sqlite3_exec(db, command, nil, nil, nil) == SQLITE_OK else { throw ArchiveError.unavailable }
        }
        func scalar(_ directory: URL, _ command: String) throws -> Data? {
            var db: OpaquePointer?, query: OpaquePointer?
            guard sqlite3_open(directory.appendingPathComponent("archive.sqlite").path, &db) == SQLITE_OK else { throw ArchiveError.unavailable }
            defer { sqlite3_finalize(query); sqlite3_close(db) }
            guard sqlite3_prepare_v2(db, command, -1, &query, nil) == SQLITE_OK else { throw ArchiveError.unavailable }
            guard sqlite3_step(query) == SQLITE_ROW else { return nil }
            guard let bytes = sqlite3_column_blob(query, 0) else { return nil }
            return Data(bytes: bytes, count: Int(sqlite3_column_bytes(query, 0)))
        }
        func legacyPayload(_ entry: ArchiveEntry) throws -> Data {
            var object = try JSONSerialization.jsonObject(with: JSONEncoder().encode(entry)) as! [String: Any]
            for key in ["categoryID", "titleRichText", "bodyRichText", "titleStyle", "bodyStyle"] { object.removeValue(forKey: key) }
            return try JSONSerialization.data(withJSONObject: object, options: [.sortedKeys])
        }
        func legacyInsert(_ entry: ArchiveEntry, payload: Data) -> String {
            let hex = payload.map { String(format: "%02x", $0) }.joined()
            let title = entry.title.replacingOccurrences(of: "'", with: "''")
            return "INSERT INTO entries VALUES('\(entry.id)','\(entry.template.rawValue)','\(title)',\(entry.date.timeIntervalSince1970),\(entry.modified.timeIntervalSince1970),\(entry.media.count),x'\(hex)');"
        }
        let legacySchema = "CREATE TABLE entries(id TEXT PRIMARY KEY, template TEXT NOT NULL, title TEXT NOT NULL, date REAL NOT NULL, modified REAL NOT NULL, mediaCount INTEGER NOT NULL, payload BLOB NOT NULL); CREATE TABLE state(key TEXT PRIMARY KEY,value TEXT NOT NULL); PRAGMA user_version=1;"
        var phase = "baseline"
        do {
            let location = root.appendingPathComponent("documents"), store = ArchiveStore(directory: location)
            check(!fm.fileExists(atPath: location.path), "Archive opens lazily without touching storage at controller construction")
            check(try store.summaries().isEmpty && store.selection() == nil, "A fresh archive has an empty gallery and no selection")
            var journal = ArchiveEntry(template: .journal, title: "旅行 Journal", date: Date(timeIntervalSince1970: 1_700_000_000),
                body: "Unicode 🧭\nNative text\0inside body", modified: Date(timeIntervalSince1970: 1_700_000_001))
            let question = ArchiveEntry(template: .research, title: "Why?", date: Date(timeIntervalSince1970: 1_700_000_002),
                body: "An answer", modified: Date(timeIntervalSince1970: 1_700_000_003))
            try store.save(journal); try store.save(question); try store.select(journal.id)
            check(try store.summaries().map(\.id) == [question.id, journal.id], "Gallery reads only bounded metadata in modified order")
            check(try store.entry(journal.id) == journal, "Unicode, body NULs and every document field round-trip")
            let reopened = ArchiveStore(directory: location)
            check(try reopened.selection() == journal.id && reopened.entry(journal.id) == journal, "Selection and selected document survive reopening")
            try reopened.select(nil)
            check(try ArchiveStore(directory: location).selection() == nil, "Returning to gallery persists nil selection")
            try reopened.select(question.id); try reopened.delete(journal.id)
            check(try reopened.selection() == question.id, "Deleting another document cannot clear the current selection")
            try reopened.delete(question.id)
            check(try reopened.selection() == nil && reopened.summaries().isEmpty, "Deleting selected document clears its selection atomically")
            try reopened.delete(question.id)
            check(try reopened.summaries().isEmpty, "Repeated deletion is idempotent")
            check(try reopened.entry(UUID()) == nil, "Missing documents are distinct from malformed documents")

            var invalid = journal; invalid.title = String(repeating: "x", count: 201)
            check(rejects { try store.save(invalid) }, "Titles above 200 characters are rejected")
            invalid = journal; invalid.title = "before\0after"
            check(rejects { try store.save(invalid) }, "Embedded title NULs cannot silently diverge between SQLite summary and JSON")
            invalid = journal; invalid.body = String(repeating: "x", count: ArchiveStore.maximumBodyBytes + 1)
            check(rejects { try store.save(invalid) }, "Oversized bodies never enter the database")
            invalid = journal; invalid.date = Date(timeIntervalSince1970: .infinity)
            check(rejects { try store.save(invalid) }, "Nonfinite timestamps are rejected before encoding")
            invalid = journal; invalid.modified = Date(timeIntervalSince1970: .nan)
            check(rejects { try store.save(invalid) }, "Nonfinite modification times are rejected")
            let original = root.appendingPathComponent("ordinary-file.dat"), sentinel = Data("Never open, copy, edit or delete this original".utf8)
            try sentinel.write(to: original)
            let reference = NotesMediaReference(kind: .image, bookmark: Data([1, 2, 3]), isSecurityScoped: false,
                lastKnownPath: original.path, displayName: original.lastPathComponent, pixelWidth: 1, pixelHeight: 1, duration: nil, frameCount: 1)
            journal.media = [reference]; try store.save(journal)
            check(try store.entry(journal.id)?.media == [reference], "Archive stores validated reference descriptors without decoding media")
            check(try fm.contentsOfDirectory(atPath: location.path).allSatisfy { $0.hasPrefix("archive.sqlite") }, "No media is duplicated into archive storage")
            try store.delete(journal.id)
            check(try Data(contentsOf: original) == sentinel, "Deleting an entry cannot delete even a non-media original referenced as media")
            invalid = journal; invalid.media = Array(repeating: reference, count: 17)
            check(rejects { try store.save(invalid) }, "Attachment count is bounded")
            invalid = journal; invalid.media[0].bookmark = Data()
            check(rejects { try store.save(invalid) }, "Invalid bookmarks are rejected without opening the original")

            phase = "categories"
            check(try store.categories().isEmpty, "Fresh archives do not create Journal or Q&A categories")
            let category = ArchiveCategory(name: "Research 研究", created: Date(timeIntervalSince1970: 1_700_000_000)), otherCategory = ArchiveCategory(name: "Travel", created: Date(timeIntervalSince1970: 1_700_000_001))
            try store.saveCategory(category); try store.saveCategory(category); try store.saveCategory(otherCategory)
            check(try ArchiveStore(directory: location).categories() == [category, otherCategory], "Custom categories persist and an identical retry is idempotent")
            check(rejects { try store.saveCategory(ArchiveCategory(name: " research 研究 ")) }, "Trimmed case-insensitive duplicate category names are rejected")
            for name in ["", "  ", "a\nb", "a\0b", String(repeating: "x", count: 81)] {
                check(rejects { try store.saveCategory(ArchiveCategory(name: name)) }, "Invalid category names never enter SQLite")
            }
            let fractionalCategory = ArchiveCategory(name: "Fractional timestamp", created: Date(timeIntervalSinceReferenceDate: 0.1234567))
            try store.saveCategory(fractionalCategory); try store.saveCategory(fractionalCategory)
            check(try store.categories().filter { $0.id == fractionalCategory.id }.count == 1, "Idempotent category retry tolerates SQLite epoch conversion precision")
            let categoryLimit = ArchiveStore(directory: root.appendingPathComponent("category-limit"))
            _ = try categoryLimit.categories()
            var categoryInserts = "BEGIN;"
            for index in 0..<ArchiveStore.maximumCategories { categoryInserts += "INSERT INTO categories VALUES('\(UUID())','Category \(index)',\(index));" }
            try sql(categoryLimit.directory, categoryInserts + "COMMIT;")
            check(try categoryLimit.categories().count == ArchiveStore.maximumCategories, "Stored categories have a fixed 100-category bound")
            check(rejects { try categoryLimit.saveCategory(ArchiveCategory(name: "Overflow")) }, "A 101st category is rejected transactionally")
            check(try categoryLimit.categories().count == ArchiveStore.maximumCategories, "Category overflow never overwrites existing categories")
            phase = "formatting"
            var rich = ArchiveEntry(template: .research, title: "文😀标题", body: "Paragraph 🧭 one\nSecond paragraph", categoryID: category.id)
            let titleStyle = NotesTextStyle(fontName: "Helvetica", fontSize: 24, color: NotesRGBA(red: 0.8, green: 0.2, blue: 0.1), bold: true, italic: true, underline: true)
            let bodyStyle = NotesTextStyle(fontSize: 16, strikethrough: true)
            rich.titleRichText = NotesRichText(runs: [NotesTextRun(location: 1, length: 2, style: titleStyle)])
            rich.bodyRichText = NotesRichText(runs: [NotesTextRun(location: 0, length: (rich.body as NSString).length, style: bodyStyle)])
            rich.titleStyle = titleStyle; rich.bodyStyle = bodyStyle; rich.media = [reference]
            try store.save(rich)
            check(try store.entry(rich.id) == rich, "UTF-16 per-range title/body formatting and empty typing styles round-trip with the canonical text")
            var moved = rich; moved.categoryID = otherCategory.id; try store.save(moved)
            check(try store.entry(rich.id) == moved, "Changing category preserves document identity, legacy template, formatting and media")
            var badRich = moved; badRich.bodyRichText!.runs[0].length += 1
            check(rejects { try store.save(badRich) }, "Out-of-bounds formatting rejects the save")
            badRich = moved; badRich.titleStyle.fontSize = .nan
            check(rejects { try store.save(badRich) }, "Invalid base typing style rejects the save")
            badRich = moved; badRich.categoryID = UUID()
            check(rejects { try store.save(badRich) }, "A document cannot commit a dangling category reference")
            check(try store.entry(rich.id) == moved, "Rejected rich-text/category saves preserve the previous complete entry")
            var emptyRich = ArchiveEntry(template: .journal, titleStyle: titleStyle, bodyStyle: bodyStyle)
            emptyRich.titleRichText = NotesRichText(); emptyRich.bodyRichText = NotesRichText(); try store.save(emptyRich)
            check(try store.entry(emptyRich.id) == emptyRich, "Empty title/body can persist the intended next-character formatting")
            check(try store.summaries().first(where: { $0.id == rich.id })?.thumbnail == reference && store.thumbnail(for: rich.id) == reference,
                  "Summary and lazy thumbnail lookup read a first-media descriptor without opening its file")

            phase = "migration"
            let legacyLocation = root.appendingPathComponent("legacy"), oldPayload = try legacyPayload(journal)
            try sql(legacyLocation, legacySchema + legacyInsert(journal, payload: oldPayload) + "INSERT INTO state VALUES('selection','\(journal.id)');")
            let migrated = ArchiveStore(directory: legacyLocation), migratedCategories = try migrated.categories()
            check(migratedCategories == [ArchiveCategory.legacy(.journal)], "Migration creates only legacy categories actually represented by existing entries")
            let migratedEntry = try migrated.entry(journal.id)
            check(migratedEntry?.id == journal.id && migratedEntry?.body == journal.body && migratedEntry?.categoryID == migratedCategories[0].id,
                  "Schema-one document identity, content and selection map safely to its legacy category")
            check(try migrated.selection() == journal.id && scalar(legacyLocation, "SELECT payload FROM entries") == oldPayload,
                  "Migration preserves original payload bytes and saved selection")
            check(migratedEntry?.titleStyle.fontSize == 17 && migratedEntry?.bodyStyle.fontSize == 12 && migratedEntry?.titleRichText == nil,
                  "Legacy JSON decodes additive formatting fields with compatible defaults")
            check(try migrated.summaries()[0].thumbnail == reference && scalar(legacyLocation, "PRAGMA user_version") == Data("2".utf8),
                  "Migration backfills thumbnail metadata and advances the schema transactionally")
            if let migratedEntry { try migrated.save(migratedEntry); check(try migrated.entry(journal.id) == migratedEntry, "The first post-migration edit saves its effective category normally") }
            phase = "empty migration"
            let emptyLegacy = root.appendingPathComponent("legacy-empty"); try sql(emptyLegacy, legacySchema)
            check(try ArchiveStore(directory: emptyLegacy).categories().isEmpty, "Empty schema-one databases migrate without inventing categories")
            phase = "failed migration"
            let badLegacy = root.appendingPathComponent("legacy-corrupt")
            try sql(badLegacy, legacySchema + legacyInsert(journal, payload: oldPayload) + legacyInsert(question, payload: Data("{".utf8)))
            let badLegacyURL = badLegacy.appendingPathComponent("archive.sqlite"), badLegacyBytes = try Data(contentsOf: badLegacyURL)
            check(rejects { _ = try ArchiveStore(directory: badLegacy).summaries() }, "One corrupt legacy record aborts the entire migration")
            check(try Data(contentsOf: badLegacyURL) == badLegacyBytes && scalar(badLegacy, "PRAGMA user_version") == Data("1".utf8),
                  "Failed migration preserves every original database byte and schema version")

            let interruptedLegacy = root.appendingPathComponent("legacy-ddl-rollback")
            try sql(interruptedLegacy, legacySchema + legacyInsert(journal, payload: oldPayload)
                + "CREATE TRIGGER reject_migration BEFORE UPDATE ON entries BEGIN SELECT RAISE(FAIL,'synthetic'); END;")
            let interruptedURL = interruptedLegacy.appendingPathComponent("archive.sqlite"), interruptedBytes = try Data(contentsOf: interruptedURL)
            check(rejects { _ = try ArchiveStore(directory: interruptedLegacy).summaries() }, "A failure during metadata backfill aborts migration after schema changes")
            check(try Data(contentsOf: interruptedURL) == interruptedBytes && scalar(interruptedLegacy, "PRAGMA user_version") == Data("1".utf8),
                  "SQLite rollback restores old schema, payloads and exact file bytes after failed DDL/backfill")
            let missingV2 = root.appendingPathComponent("missing-v2")
            try sql(missingV2, legacySchema + "PRAGMA user_version=2;")
            let missingV2URL = missingV2.appendingPathComponent("archive.sqlite"), missingV2Bytes = try Data(contentsOf: missingV2URL)
            check(rejects { _ = try ArchiveStore(directory: missingV2).summaries() }, "An incomplete schema-two database is not silently repaired")
            check(try Data(contentsOf: missingV2URL) == missingV2Bytes, "Malformed current-schema bytes remain unchanged")
            phase = "thumbnail bound"
            let thumbnailLocation = root.appendingPathComponent("thumbnails"), thumbnailStore = ArchiveStore(directory: thumbnailLocation)
            var largeReference = reference; largeReference.bookmark = Data(repeating: 7, count: 1024 * 1024)
            var thumbnailIDs: [UUID] = []
            for _ in 0..<13 { let entry = ArchiveEntry(template: .journal, media: [largeReference]); thumbnailIDs.append(entry.id); try thumbnailStore.save(entry) }
            let thumbnailSummaries = try thumbnailStore.summaries()
            let retainedBytes = try thumbnailSummaries.compactMap(\.thumbnail).reduce(0) { try $0 + JSONEncoder().encode($1).count }
            check(retainedBytes <= ArchiveStore.maximumSummaryThumbnailBytes && thumbnailSummaries.contains { $0.thumbnail == nil && $0.mediaCount == 1 },
                  "Metadata gallery descriptors have a 16 MiB aggregate bound without truncating documents")
            let omittedID = thumbnailSummaries.first(where: { $0.thumbnail == nil })!.id
            check(try thumbnailStore.thumbnail(for: omittedID) == largeReference && thumbnailStore.entry(omittedID)?.media == [largeReference],
                  "A descriptor omitted by the metadata cache budget remains lazily recoverable from its unchanged document")
            try sql(thumbnailLocation, "UPDATE entries SET payload=x'7B' WHERE id='\(omittedID)'")
            check(try thumbnailStore.summaries().count == 13 && thumbnailStore.thumbnail(for: omittedID) == largeReference,
                  "Gallery thumbnails never require deserializing every document body")

            phase = "existing corruption"
            let future = root.appendingPathComponent("future")
            try sql(future, "CREATE TABLE future_content(value TEXT); INSERT INTO future_content VALUES('keep'); PRAGMA user_version=27")
            let futureURL = future.appendingPathComponent("archive.sqlite"), futureBytes = try Data(contentsOf: futureURL)
            check(rejects { _ = try ArchiveStore(directory: future).summaries() }, "Future schema is rejected")
            check(try Data(contentsOf: futureURL) == futureBytes, "Future schema and original bytes are preserved")
            let foreign = root.appendingPathComponent("foreign")
            try sql(foreign, "CREATE TABLE unrelated(value TEXT); INSERT INTO unrelated VALUES('keep')")
            let foreignURL = foreign.appendingPathComponent("archive.sqlite"), foreignBytes = try Data(contentsOf: foreignURL)
            check(rejects { _ = try ArchiveStore(directory: foreign).summaries() }, "Foreign version-zero database is not initialized as Archive")
            check(try Data(contentsOf: foreignURL) == foreignBytes, "Foreign content is unchanged")
            let damaged = root.appendingPathComponent("damaged")
            try fm.createDirectory(at: damaged, withIntermediateDirectories: true)
            let damagedURL = damaged.appendingPathComponent("archive.sqlite"), damagedBytes = Data("This is not SQLite".utf8)
            try damagedBytes.write(to: damagedURL)
            check(rejects { _ = try ArchiveStore(directory: damaged).summaries() }, "Corrupt database is not replaced with an empty archive")
            check(try Data(contentsOf: damagedURL) == damagedBytes, "Corrupt original bytes are retained")
            let badPayload = root.appendingPathComponent("payload"), payloadStore = ArchiveStore(directory: badPayload)
            try payloadStore.save(question)
            try sql(badPayload, "UPDATE entries SET payload=x'7B'")
            check(try payloadStore.summaries().count == 1, "Gallery does not deserialize every document body")
            check(rejects { _ = try payloadStore.entry(question.id) }, "Malformed selected payload reports a validation failure")
            try payloadStore.save(question); try sql(badPayload, "UPDATE entries SET title='tampered'")
            check(rejects { _ = try payloadStore.entry(question.id) }, "Metadata and payload mismatch is rejected")
            try payloadStore.save(question); try sql(badPayload, "UPDATE entries SET mediaCount=99")
            check(rejects { _ = try payloadStore.summaries() }, "Malformed gallery media counts are rejected")
            try payloadStore.save(question); try sql(badPayload, "INSERT OR REPLACE INTO state VALUES('selection','not-a-uuid')")
            check(rejects { _ = try payloadStore.selection() }, "Invalid saved selection is not silently treated as gallery")
            let missingSchema = root.appendingPathComponent("missing-schema")
            try sql(missingSchema, "PRAGMA user_version=1")
            check(rejects { _ = try ArchiveStore(directory: missingSchema).summaries() }, "Missing version-one tables are reported without silent repair")

            let diagnosticA = root.appendingPathComponent("Notes-UUID-A"), diagnosticB = root.appendingPathComponent("Notes-UUID-B")
            for argument in ["--ui-test", "--render-archive", "--archive-smoke-test"] {
                let a = ArchiveStore.directory(notesDirectory: diagnosticA, arguments: [argument])
                let b = ArchiveStore.directory(notesDirectory: diagnosticB, arguments: [argument])
                check(a != b && a.deletingLastPathComponent().path == diagnosticA.path, "Each diagnostic flag preserves its unique process root: " + argument)
            }
            check(ArchiveStore.directory(notesDirectory: root.appendingPathComponent("Notes"), arguments: []) == root.appendingPathComponent("Archive", isDirectory: true),
                  "Normal Archive storage remains a sibling of Notes")
            check(ArchiveStore.applicationDirectory() == ArchiveStore.applicationDirectory(), "Process directory is stable across reopening")

            phase = "existing controller"
            let controlledLocation = root.appendingPathComponent("controlled"), controlledStore = ArchiveStore(directory: controlledLocation)
            let schedule = Scheduler(), controller = schedule.controller(controlledStore)
            var events: [String] = []; controller.onEvent = { events.append($0) }
            controller.create(.journal)
            let created = controller.selected!
            check(controller.isBusy && controller.hasUnsavedChanges && events.isEmpty, "Creation is dirty until acknowledged and does not emit success early")
            for index in 0..<40 { var entry = controller.selected!; entry.body = "Edit \(index)"; controller.update(entry) }
            check(schedule.work.count == 2, "Typing forty revisions does not queue forty SQLite writes")
            schedule.execute(); schedule.deliver()
            check(controller.hasUnsavedChanges && schedule.work.count == 2, "Acknowledging an older save keeps and schedules the newest revision")
            controller.deactivate(); schedule.drain()
            check(try controlledStore.entry(created.id)?.body == "Edit 39", "Close flushes the final revision even when an earlier write was in flight")
            check(!controller.isBusy && !controller.hasUnsavedChanges && events == ["created"], "All write acknowledgments settle once without duplicate creation events")
            controller.activate(); schedule.drain()
            check(controller.selected?.body == "Edit 39", "Reopening restores persisted selection and final text")
            controller.select(nil); schedule.drain()
            check(try controller.selected == nil && controlledStore.selection() == nil, "Gallery navigation is persisted")
            controller.select(created.id); schedule.drain()
            var deleteEdit = controller.selected!; deleteEdit.body = "Write already queued"; controller.update(deleteEdit); controller.flush()
            controller.deleteSelected()
            check(controller.selected == nil && controller.entries.isEmpty, "Delete immediately invalidates the editor and gallery card")
            deleteEdit.body = "Stale editor must never resurrect"; controller.update(deleteEdit); controller.select(created.id)
            check(controller.selected == nil, "In-flight deletion rejects stale editor updates and reselection")
            schedule.drain()
            check(try controlledStore.entry(created.id) == nil && !controller.hasUnsavedChanges && events.last == "deleted", "FIFO deletion wins over preexisting saves without resurrection")

            // SQLite triggers inject failures without changing permissions or touching real files.
            controller.create(.research); schedule.drain()
            let retainedID = controller.selected!.id
            try sql(controlledLocation, "CREATE TRIGGER reject_write BEFORE INSERT ON entries BEGIN SELECT RAISE(FAIL, 'synthetic'); END")
            var unsaved = controller.selected!; unsaved.body = "Last edit survives failure and hidden state"; controller.update(unsaved)
            controller.deactivate(); schedule.drain()
            check(controller.error != nil && controller.hasUnsavedChanges && schedule.work.isEmpty, "Hidden write failure remains visible, retains data and does not loop")
            check(try controlledStore.entry(retainedID)?.body != unsaved.body, "Failed commit does not falsely appear persisted")
            controller.activate(); schedule.drain()
            check(controller.selected?.body == unsaved.body && controller.error != nil, "Reopening overlays unsaved snapshot on older persisted data")
            let countBefore = controller.entries.count
            controller.create(.journal)
            check(controller.entries.count == countBefore, "Storage failure cannot accumulate unbounded new drafts")
            try sql(controlledLocation, "DROP TRIGGER reject_write")
            controller.retryPendingSaves(); schedule.drain()
            check(try controlledStore.entry(retainedID)?.body == unsaved.body && !controller.hasUnsavedChanges && controller.error == nil, "Explicit retry persists retained latest draft and clears save error")

            try sql(controlledLocation, "CREATE TRIGGER reject_delete BEFORE DELETE ON entries BEGIN SELECT RAISE(FAIL, 'synthetic'); END")
            controller.deleteSelected(); schedule.drain()
            check(controller.selected == nil && controller.entries.contains { $0.id == retainedID } && controller.error != nil,
                  "Failed deletion restores the gallery entry with an error")
            check(try controlledStore.entry(retainedID) != nil, "Failed deletion preserves the original database entry")
            try sql(controlledLocation, "DROP TRIGGER reject_delete")
            controller.select(retainedID); schedule.drain(); controller.deleteSelected(); schedule.drain()
            check(try controlledStore.entry(retainedID) == nil && controller.error == nil, "Retrying deletion succeeds and clears its failure")

            controller.create(.journal); schedule.drain()
            var final = controller.selected!; final.body = "Final shutdown text"; controller.update(final)
            var drained: Bool?
            controller.drainPendingWrites { drained = $0 }
            check(drained == nil && controller.isBusy, "Shutdown completion waits for actual commit acknowledgement")
            schedule.drain()
            check(try drained == true && controlledStore.entry(final.id)?.body == final.body, "Shutdown barrier reports success only after final data is durable")
            try sql(controlledLocation, "CREATE TRIGGER reject_write BEFORE INSERT ON entries BEGIN SELECT RAISE(FAIL, 'synthetic'); END")
            final.body = "Failed shutdown"; controller.update(final); drained = nil
            controller.drainPendingWrites { drained = $0 }; schedule.drain()
            check(drained == false && controller.hasUnsavedChanges, "Failed shutdown completion preserves draft and honestly reports failure")
            try sql(controlledLocation, "DROP TRIGGER reject_write")
            controller.retryPendingSaves(); schedule.drain()
            check(try controlledStore.entry(final.id)?.body == final.body, "Failed shutdown data can still be retried")
            controller.deactivate(); schedule.drain()

            phase = "category controller"
            let categorySchedule = Scheduler(), categoryStore = ArchiveStore(directory: root.appendingPathComponent("category-controller"))
            let categoryController = categorySchedule.controller(categoryStore)
            var categoryEvents: [String] = []; categoryController.onEvent = { categoryEvents.append($0) }
            categoryController.activate(); categorySchedule.drain()
            let newCategoryID = categoryController.createCategory(named: "  Notebook  ")!
            categoryController.create(categoryID: newCategoryID)
            let categorizedID = categoryController.selected!.id
            check(categoryController.hasUnsavedChanges && categoryEvents.isEmpty && categoryController.categories[0].name == "Notebook",
                  "New category IDs are available immediately while success events await commit")
            categorySchedule.drain()
            check(try categoryStore.entry(categorizedID)?.categoryID == newCategoryID && categoryStore.categories().map(\.id) == [newCategoryID],
                  "The FIFO writer commits a category before an immediately created referencing document")
            check(categoryEvents == ["categoryCreated", "created"] && !categoryController.hasUnsavedChanges,
                  "Category and document success events are emitted once after their own commits")
            check(categoryController.createCategory(named: "notebook") == newCategoryID && categorySchedule.work.isEmpty,
                  "Choosing an existing normalized category creates no duplicate or extra write")
            let destination = categoryController.createCategory(named: "Second")!
            categoryController.moveSelected(to: destination)
            var concurrentEdit = categoryController.selected!; concurrentEdit.body = "Preserved during category move"; categoryController.update(concurrentEdit)
            categorySchedule.drain(); categoryController.flush(); categorySchedule.drain()
            check(try categoryStore.entry(categorizedID)?.categoryID == destination && categoryStore.entry(categorizedID)?.body == concurrentEdit.body,
                  "Moving a document and editing during its write preserves the newest category and text together")
            check(categoryEvents.filter { $0 == "categoryChanged" }.count == 1, "A category move emits a single acknowledged action across coalesced revisions")
            categoryController.moveSelected(to: nil); categorySchedule.drain()
            check(try categoryStore.entry(categorizedID)?.categoryID == nil, "Documents may return to uncategorized without losing their template")
            try sql(categoryStore.directory, "CREATE TRIGGER reject_category BEFORE INSERT ON categories BEGIN SELECT RAISE(FAIL,'synthetic'); END")
            let retryID = categoryController.createCategory(named: "Retained")!
            categoryController.create(categoryID: retryID); let retryDocumentID = categoryController.selected!.id
            categoryController.deactivate(); categorySchedule.drain()
            check(categoryController.error != nil && categoryController.hasUnsavedChanges && categorySchedule.work.isEmpty,
                  "Failed category and dependent document writes retain drafts while hidden without retry loops")
            check(try !categoryStore.categories().contains { $0.id == retryID } && categoryStore.entry(retryDocumentID) == nil,
                  "A failed category insertion cannot leave an invalid document reference committed")
            try sql(categoryStore.directory, "DROP TRIGGER reject_category")
            var categoryDrained: Bool?
            categoryController.drainPendingWrites { categoryDrained = $0 }; categorySchedule.drain()
            check(try categoryDrained == true && categoryStore.entry(retryDocumentID)?.categoryID == retryID && !categoryController.hasUnsavedChanges,
                  "Shutdown retry includes category drafts before dependent documents and reports durable success")
            check(categoryEvents.filter { $0 == "categoryCreated" }.count == 3, "Failed category attempts are not logged as successful creations")

            categoryController.activate(); categorySchedule.drain()
            let outgoingDocument = categoryController.selected!
            categoryController.select(categorizedID)
            check(categoryController.selected?.id == outgoingDocument.id, "Switching documents keeps outgoing content until the selected replacement has loaded")
            categorySchedule.drain()
            check(categoryController.selected?.id == categorizedID, "Successful document switch replaces content once without an intermediate gallery state")
            categoryController.select(retryDocumentID); categoryController.select(nil)
            check(categoryController.selected == nil, "Explicit Back clears selected content immediately")
            categorySchedule.drain()
            check(categoryController.selected == nil, "A late document result cannot undo newer gallery navigation")
            categoryController.select(categorizedID); categorySchedule.drain()
            try sql(categoryStore.directory, "UPDATE entries SET payload=x'7B' WHERE id='\(retryDocumentID)'")
            categoryController.select(retryDocumentID)
            var editedOutgoing = categoryController.selected!; editedOutgoing.body = "Typing during the pending document switch"
            categoryController.update(editedOutgoing); categorySchedule.drain()
            check(categoryController.selected?.body == editedOutgoing.body, "A failed incoming load cannot overwrite edits made to the retained outgoing document while waiting")
            check(categoryController.selected?.id == categorizedID && categoryController.error != nil,
                  "A failed incoming document load preserves the readable outgoing document and reports the error")
            categoryController.deactivate(); categorySchedule.drain()

            phase = "category deletion"
            let deletionStore = ArchiveStore(directory: root.appendingPathComponent("category-deletion"))
            let removable = ArchiveCategory(name: "Remove me"), keptCategory = ArchiveCategory(name: "Keep me")
            try deletionStore.saveCategory(removable); try deletionStore.saveCategory(keptCategory)
            var removedCategoryEntry = rich; removedCategoryEntry.categoryID = removable.id
            var untouchedEntry = question; untouchedEntry.categoryID = keptCategory.id
            try deletionStore.save(removedCategoryEntry); try deletionStore.save(untouchedEntry); try deletionStore.select(removedCategoryEntry.id)
            try sql(deletionStore.directory, "CREATE TRIGGER reject_category_removal BEFORE DELETE ON categories BEGIN SELECT RAISE(FAIL,'synthetic'); END")
            let beforeRemoval = try Data(contentsOf: deletionStore.directory.appendingPathComponent("archive.sqlite"))
            check(rejects { try deletionStore.deleteCategory(removable.id) }, "Category removal reports a failed final delete after its document updates")
            check(try deletionStore.entry(removedCategoryEntry.id) == removedCategoryEntry && deletionStore.categories().count == 2
                    && Data(contentsOf: deletionStore.directory.appendingPathComponent("archive.sqlite")) == beforeRemoval,
                  "Failed category deletion rolls back document payloads, category metadata and original database bytes together")
            try sql(deletionStore.directory, "DROP TRIGGER reject_category_removal")
            try deletionStore.deleteCategory(removable.id)
            removedCategoryEntry.categoryID = nil
            check(try deletionStore.entry(removedCategoryEntry.id) == removedCategoryEntry && deletionStore.entry(untouchedEntry.id) == untouchedEntry,
                  "Deleting a category preserves IDs, dates, text, mixed formatting and media while moving only its documents to Uncategorized")
            check(try deletionStore.selection() == removedCategoryEntry.id && deletionStore.categories().map(\.id) == [keptCategory.id],
                  "Category deletion preserves the selected document and every other category")
            try deletionStore.deleteCategory(removable.id)
            check(try deletionStore.summaries().count == 2, "Repeating a completed category removal is idempotent and never deletes documents")

            let deletionSchedule = Scheduler(), deletionController = deletionSchedule.controller(ArchiveStore(directory: root.appendingPathComponent("category-deletion-controller")))
            var removalEvents: [String] = []; deletionController.onEvent = { removalEvents.append($0) }
            deletionController.activate(); deletionSchedule.drain()
            let pendingCategory = deletionController.createCategory(named: "Pending")!
            deletionController.create(categoryID: pendingCategory)
            var firstDraft = deletionController.selected!; firstDraft.body = "First unsaved document"; deletionController.update(firstDraft)
            deletionController.create(categoryID: pendingCategory)
            var secondDraft = deletionController.selected!; secondDraft.body = "Second unsaved document"; deletionController.update(secondDraft)
            deletionController.deleteCategory(pendingCategory)
            check(deletionController.categories.isEmpty && deletionController.entries.count == 2 && deletionController.entries.allSatisfy { $0.categoryID == nil }
                    && deletionController.selected?.categoryID == nil && deletionController.hasUnsavedChanges,
                  "Confirmed category removal keeps all pending documents visible as Uncategorized before acknowledgement")
            var afterRemoval = deletionController.selected!; afterRemoval.body = "Edited while category removal was queued"; deletionController.update(afterRemoval)
            deletionSchedule.drain()
            check(try deletionController.store.entry(firstDraft.id)?.body == firstDraft.body
                    && deletionController.store.entry(secondDraft.id)?.body == afterRemoval.body
                    && deletionController.store.summaries().allSatisfy { $0.categoryID == nil },
                  "FIFO category removal absorbs multiple unsaved drafts and preserves a newer edit arriving during its transaction")
            check(!deletionController.hasUnsavedChanges && removalEvents.filter { $0 == "categoryDeleted" }.count == 1,
                  "Successful category removal emits exactly one closed action after commit")
            let failingCategory = deletionController.createCategory(named: "Recoverable")!
            deletionController.moveSelected(to: failingCategory); deletionSchedule.drain()
            let persistedBeforeFailure = try deletionController.store.entry(secondDraft.id)!
            try sql(deletionController.store.directory, "CREATE TRIGGER reject_category_removal BEFORE DELETE ON categories BEGIN SELECT RAISE(FAIL,'synthetic'); END")
            var failureDraft = deletionController.selected!; failureDraft.body = "Keep this draft through category deletion failure"; deletionController.update(failureDraft)
            deletionController.deleteCategory(failingCategory); deletionSchedule.drain()
            check(deletionController.error != nil && deletionController.hasUnsavedChanges && deletionSchedule.work.isEmpty,
                  "A failed category removal retains its intent and dirty text without automatic retry loops")
            check(try deletionController.store.entry(secondDraft.id) == persistedBeforeFailure && deletionController.store.categories().contains { $0.id == failingCategory },
                  "Failed removal cannot partially commit the affected unsaved document or silently discard its stored category")
            deletionController.deactivate(); deletionSchedule.drain(); deletionController.activate(); deletionSchedule.drain()
            check(deletionController.selected?.body == failureDraft.body && deletionController.selected?.categoryID == nil
                    && !deletionController.categories.contains { $0.id == failingCategory },
                  "Reopening overlays retained removal intent and newest drafts on unchanged persisted records")
            try sql(deletionController.store.directory, "DROP TRIGGER reject_category_removal")
            var removalDrained: Bool?
            deletionController.drainPendingWrites { removalDrained = $0 }; deletionSchedule.drain()
            check(try removalDrained == true && deletionController.store.entry(secondDraft.id)?.body == failureDraft.body
                    && deletionController.store.entry(secondDraft.id)?.categoryID == nil && deletionController.error == nil,
                  "Shutdown retry atomically completes failed category removal with its retained text and reports durable success")
            let lastCategory = deletionController.createCategory(named: "Delete alongside document")!
            deletionController.moveSelected(to: lastCategory); deletionSchedule.drain()
            deletionController.deleteCategory(lastCategory); deletionController.deleteSelected(); deletionSchedule.drain()
            check(try deletionController.store.entry(secondDraft.id) == nil && deletionController.store.entry(firstDraft.id) != nil,
                  "Deleting a document behind an in-flight category removal wins without resurrecting that document")
            deletionController.deactivate(); deletionSchedule.drain()

            phase = "thumbnail controller"
            let thumbnailSchedule = Scheduler(), thumbnailController = thumbnailSchedule.controller(store)
            thumbnailController.activate(); thumbnailSchedule.drain()
            var thumbnailResults: [NotesMediaReference?] = []
            thumbnailController.loadThumbnail(for: rich.id) { thumbnailResults.append($0) }
            thumbnailController.loadThumbnail(for: rich.id) { thumbnailResults.append($0) }
            check(thumbnailSchedule.work.count == 1, "Same-ID visible thumbnail requests coalesce into one metadata read")
            thumbnailSchedule.drain()
            check(thumbnailResults.count == 2 && thumbnailResults.allSatisfy { $0 == reference }, "Coalesced callers receive the validated reference")
            thumbnailResults.removeAll()
            thumbnailController.loadThumbnail(for: rich.id) { thumbnailResults.append($0) }
            thumbnailController.deactivate()
            check(thumbnailResults.count == 1 && thumbnailResults[0] == nil, "Closing cancels outstanding visible thumbnail callbacks immediately")
            thumbnailController.activate()
            thumbnailController.loadThumbnail(for: rich.id) { thumbnailResults.append($0) }
            thumbnailSchedule.drain()
            check(thumbnailResults.count == 2 && thumbnailResults[1] == reference,
                  "A stale callback cannot remove or cancel a newly reopened request for the same thumbnail")
            thumbnailController.deactivate(); thumbnailSchedule.drain()

            phase = "remaining controller"
            let ownedSchedule = Scheduler(), ownedStore = ArchiveStore(directory: root.appendingPathComponent("owned"))
            var owned: ArchiveController? = ownedSchedule.controller(ownedStore)
            weak var retainedController = owned
            owned!.create(.journal); let ownedID = owned!.selected!.id
            var ownedEntry = owned!.selected!; ownedEntry.body = "Owner released before commit"; owned!.update(ownedEntry)
            owned!.deactivate(); owned = nil
            check(retainedController != nil, "Queued writes own their state through commit after the HUD releases its controller")
            ownedSchedule.drain()
            check(try ownedStore.entry(ownedID)?.body == ownedEntry.body && retainedController == nil,
                  "Final revision persists and queue ownership releases after acknowledgments")

            let timeoutSchedule = Scheduler(), timeoutController = timeoutSchedule.controller(ArchiveStore(directory: root.appendingPathComponent("timeout")))
            timeoutController.create(.journal)
            var timeoutResults: [Bool] = []
            timeoutController.drainPendingWrites(timeout: 0) { timeoutResults.append($0) }
            RunLoop.main.run(until: Date().addingTimeInterval(0.015))
            check(timeoutResults == [false], "Shutdown wait has a finite deadline if worker completion is delayed")
            timeoutSchedule.drain()
            check(timeoutResults == [false] && !timeoutController.hasUnsavedChanges,
                  "A late commit finishes safely without double-calling timed-out completion")

            // Simulate AppKit's nested termination loop while the originating
            // main-dispatch callback is still on the stack. Main GCD delivery
            // would deadlock here; run-loop delivery must acknowledge normally.
            var nestedFinished = false, nestedCommit = false, nestedDeadline = false
            DispatchQueue.main.async {
                let nestedStore = ArchiveStore(directory: root.appendingPathComponent("nested-termination"))
                let nested = ArchiveController(store: nestedStore)
                nested.create(.journal)
                nested.drainPendingWrites(timeout: 0.5) { nestedCommit = $0 }
                let until = Date().addingTimeInterval(1)
                while !nestedCommit, Date() < until { RunLoop.main.run(until: Date().addingTimeInterval(0.005)) }
                var heldWork: [() -> Void] = []
                let stalled = ArchiveController(store: ArchiveStore(directory: root.appendingPathComponent("nested-deadline")), work: { heldWork.append($0) })
                stalled.create(.journal)
                stalled.drainPendingWrites(timeout: 0.01) { nestedDeadline = !$0 }
                let deadline = Date().addingTimeInterval(0.5)
                while !nestedDeadline, Date() < deadline { RunLoop.main.run(until: Date().addingTimeInterval(0.005)) }
                for item in heldWork { item() }; heldWork.removeAll()
                let cleanup = Date().addingTimeInterval(0.5)
                while stalled.isBusy, Date() < cleanup { RunLoop.main.run(until: Date().addingTimeInterval(0.005)) }
                nestedFinished = true
            }
            let nestedUntil = Date().addingTimeInterval(3)
            while !nestedFinished, Date() < nestedUntil { RunLoop.main.run(until: Date().addingTimeInterval(0.01)) }
            check(nestedFinished && nestedCommit, "Production commit acknowledgment completes inside a nested main-queue termination run loop")
            check(nestedDeadline, "Bounded drain deadline also fires inside a nested main-queue termination run loop")

            phase = "capacity"
            let fullStore = ArchiveStore(directory: root.appendingPathComponent("full"))
            let first = ArchiveEntry(template: .journal); try fullStore.save(first)
            var inserts = "BEGIN;"
            let encoder = JSONEncoder()
            for _ in 1..<ArchiveStore.maximumEntries {
                let entry = ArchiveEntry(template: .journal)
                let hex = try encoder.encode(entry).map { String(format: "%02x", $0) }.joined()
                inserts += "INSERT INTO entries(id,template,title,date,modified,mediaCount,payload) VALUES('\(entry.id)','journal','',\(entry.date.timeIntervalSince1970),\(entry.modified.timeIntervalSince1970),0,x'\(hex)');"
            }
            inserts += "COMMIT;"; try sql(fullStore.directory, inserts)
            check(try fullStore.summaries().count == ArchiveStore.maximumEntries, "Gallery storage is bounded at 2,000 documents")
            check(rejects { try fullStore.save(ArchiveEntry(template: .research)) }, "A 2,001st document is rejected transactionally")
            var editedFirst = first; editedFirst.body = "Existing edits still work"; try fullStore.save(editedFirst)
            check(try fullStore.entry(first.id) == editedFirst, "A full archive still permits editing an existing document")
        } catch { fatalError("Archive test failed during \(phase): \(error)") }
        return count
    }
}
