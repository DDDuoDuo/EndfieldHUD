import Foundation

enum SystemEventLogTests {
    static func run() -> Int {
        var count = 0
        func check(_ condition: Bool, _ message: String, file: StaticString = #file, line: UInt = #line) {
            count += 1; if !condition { fatalError(message, file: file, line: line) }
        }
        let oldLanguage = L10n.language
        L10n.language = .english
        defer { L10n.language = oldLanguage }
        let store = SystemEventLog(capacity: 3)
        var notifications = 0
        let observer = store.observe { notifications += 1 }
        check(store.events.isEmpty && store.statusMessage == nil && store.capacity == 3, "Memory store begins empty")
        store.record(kind: .overlayOpened, metadata: ["content": "Never retain this", "module": "notes"])
        let firstID = store.events[0].id
        check(store.events[0].metadata.isEmpty && store.events[0].kind == .overlayOpened, "Overlay event ignores arbitrary content")
        store.record(kind: .moduleOpened, metadata: ["module": "notes", "text": "private note"])
        check(store.events[0].metadata == ["module": "notes"] && store.events[0].detail == "Notes", "Module IDs map to localized titles")
        store.record(kind: .clipboardCopied, metadata: ["kind": "text", "text": "SECRET", "url": "https://private.example"])
        check(store.events[0].metadata == ["kind": "text"] && store.events[0].detail == "Text", "Clipboard events store only content kind")
        store.record(kind: .shelfAdded, metadata: ["filename": "/Users/person/private/report.pdf", "path": "/private"])
        check(store.events.count == 3 && !store.events.contains { $0.id == firstID }, "Capacity evicts oldest events")
        check(store.events[0].metadata == ["filename": "report.pdf"], "File events reduce paths to basename")
        store.record(kind: .shelfRemoved, metadata: ["filename": "C:\\private\\中文文件.pdf"])
        check(store.events[0].metadata["filename"] == "中文文件.pdf", "Windows path separators are also removed")
        store.record(kind: .shelfAdded, metadata: ["filename": "https://secret.example/token"])
        check(store.events[0].metadata.isEmpty, "A URL cannot masquerade as a filename")
        store.record(kind: .audioDeviceConnected, metadata: ["device": "Speaker\n\tName\u{0}", "bundle": "private.app"])
        check(store.events[0].metadata == ["device": "Speaker Name"], "Device fields flatten whitespace and drop unknown keys")
        store.record(kind: .displayConnected, metadata: ["device": "/Users/private/Display"])
        check(store.events[0].metadata.isEmpty, "Full paths are rejected as device names")
        store.record(kind: .shelfAdded, metadata: ["filename": String(repeating: "👩🏽‍💻", count: 100)])
        check((store.events[0].metadata["filename"]?.utf8.count ?? 999) <= 160, "Unicode field caps preserve complete graphemes")
        store.record(kind: .shelfAdded, metadata: ["filename": String(repeating: "a", count: 5000)])
        check(store.events[0].metadata.isEmpty, "Oversized input fields are omitted")
        store.record(kind: .workStarted, metadata: ["kind": "countdown", "seconds": "1500", "note": "private"])
        check(store.events[0].detail == "Countdown · 00:25:00" && store.events[0].metadata.count == 2, "Work metadata renders duration and mode")
        store.record(kind: .workPaused, metadata: ["kind": "text", "seconds": "-1"])
        check(store.events[0].metadata.isEmpty, "Wrong event schemas and negative durations are rejected")
        store.record(kind: .batteryStateChanged, metadata: ["state": "charging", "percentage": "101"])
        check(store.events[0].detail == "Charging" && store.events[0].metadata.count == 1, "Invalid percentages never reach the log")
        store.record(kind: .powerDisconnected, metadata: ["state": "battery", "percentage": "55"])
        check(store.events[0].detail == "On battery · 55%", "Power details preserve normalized state and percentage")
        store.record(kind: .shelfCleared, metadata: ["count": "12"])
        check(store.events[0].detail == "12 items", "Shelf clear records only count")
        let ids = store.events.map(\.id)
        L10n.language = .simplifiedChinese
        check(store.events[0].title == "File shelf cleared" && store.events[0].detail == "12 items" && store.events[0].category.title == "Files", "Log entries remain English in Chinese app mode")
        check(ids == store.events.map(\.id), "Language changes preserve persisted identity")
        L10n.language = .english
        for kind in SystemEventKind.allCases {
            check(!kind.title.isEmpty && !kind.category.title.isEmpty, "Every event kind has a title and category")
        }
        check(notifications == 15, "Each record produces one observer notification")
        store.removeObserver(observer)
        store.clear()
        check(store.events.isEmpty && notifications == 15, "Clear stays empty and removed observers remain silent")
        check(store.flushSynchronously(), "Memory stores can flush without disk access")
        check(SystemEventLog(capacity: 0).capacity == 1 && SystemEventLog(capacity: 10000).capacity == 500, "Both capacity bounds are enforced")

        let shortcutLog = SystemEventLog()
        shortcutLog.record(kind: .appShortcutOpened, metadata: ["app": "TextEdit", "path": "/private/TextEdit.app"])
        check(shortcutLog.events[0].category == .navigation && shortcutLog.events[0].detail == "TextEdit"
              && shortcutLog.events[0].metadata == ["app": "TextEdit"], "Shortcut events keep the display name only")
        shortcutLog.record(kind: .appShortcutOpened, metadata: ["app": "/private/TextEdit.app"])
        check(shortcutLog.events[0].metadata.isEmpty, "App shortcut log does not retain paths as display names")

        let root = FileManager.default.temporaryDirectory.appendingPathComponent("EndfieldEventLogTests-\(UUID().uuidString)", isDirectory: true)
        defer { try? FileManager.default.removeItem(at: root) }
        do {
            let directory = root.appendingPathComponent("persisted", isDirectory: true)
            let file = directory.appendingPathComponent("events.json")
            let persisted = SystemEventLog(directory: directory, capacity: 10)
            persisted.record(kind: .moduleOpened, metadata: ["module": "eventLog"])
            persisted.record(kind: .workCompleted, metadata: ["kind": "stopwatch", "seconds": "123"])
            let savedIDs = persisted.events.map(\.id)
            check(persisted.flushSynchronously(), "Synchronous termination flush saves an atomic document")
            let savedDate = Date(timeIntervalSince1970: 1_600_000_000)
            try FileManager.default.setAttributes([.modificationDate: savedDate], ofItemAtPath: file.path)
            check(try persisted.flushSynchronously()
                  && (FileManager.default.attributesOfItem(atPath: file.path)[.modificationDate] as? Date) == savedDate,
                  "Flushing an already saved log revision performs no redundant atomic file replacement")
            let restored = SystemEventLog(directory: directory, capacity: 10)
            check(restored.events.map(\.id) == savedIDs && restored.events == persisted.events, "Reload restores order, identity, timestamps and metadata")
            check((try JSONSerialization.jsonObject(with: Data(contentsOf: file)) as? [String: Any])?["version"] as? Int == 1, "Persistent document carries an explicit schema version")
            persisted.record(kind: .powerConnected)
            persisted.clear()
            var completed: Bool?
            persisted.flush { completed = $0 }
            let deadline = Date().addingTimeInterval(2)
            while completed == nil && Date() < deadline { RunLoop.main.run(until: Date().addingTimeInterval(0.01)) }
            check(completed == true && persisted.events.isEmpty, "Asynchronous flush confirms clear completion on the main run loop")
            RunLoop.main.run(until: Date().addingTimeInterval(0.4))
            check(SystemEventLog(directory: directory).events.isEmpty, "Canceled debounced snapshots cannot resurrect cleared entries")
            let corruptDirectory = root.appendingPathComponent("corrupt", isDirectory: true)
            try FileManager.default.createDirectory(at: corruptDirectory, withIntermediateDirectories: true)
            let corruptURL = corruptDirectory.appendingPathComponent("events.json"), corruptBytes = Data("not valid json".utf8)
            try corruptBytes.write(to: corruptURL)
            let corrupt = SystemEventLog(directory: corruptDirectory)
            corrupt.record(kind: .overlayOpened)
            check(corrupt.statusMessage != nil && !corrupt.flushSynchronously(), "Corrupt storage keeps session events with an explicit persistence error")
            check(try Data(contentsOf: corruptURL) == corruptBytes, "Corrupt originals are never overwritten")
            corrupt.clear(); check(corrupt.events.isEmpty && !corrupt.flushSynchronously(), "Clearing a corrupt session still preserves the original file")
            let futureDirectory = root.appendingPathComponent("future", isDirectory: true)
            try FileManager.default.createDirectory(at: futureDirectory, withIntermediateDirectories: true)
            let futureURL = futureDirectory.appendingPathComponent("events.json"), futureBytes = Data("{\"version\":2,\"events\":[{\"kind\":\"futureKind\"}]}".utf8)
            try futureBytes.write(to: futureURL)
            let future = SystemEventLog(directory: futureDirectory)
            check(future.statusMessage?.contains("newer format") == true && !future.flushSynchronously(), "Future schema is detected before unknown event decoding")
            check(try Data(contentsOf: futureURL) == futureBytes, "Newer documents are preserved byte for byte")
            let parentFile = root.appendingPathComponent("not-a-directory")
            try Data("original".utf8).write(to: parentFile)
            let unwritable = SystemEventLog(directory: parentFile.appendingPathComponent("log"))
            unwritable.record(kind: .overlayOpened)
            check(!unwritable.flushSynchronously() && unwritable.events.count == 1 && unwritable.statusMessage != nil, "Write failures retain session events and report failure")
            check(try String(contentsOf: parentFile, encoding: .utf8) == "original", "Persistence errors cannot replace unrelated files")
        } catch { fatalError("Event log fixture failed: \(error)") }
        return count
    }
}
