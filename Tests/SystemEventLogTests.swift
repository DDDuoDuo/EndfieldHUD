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
        let accounts = SystemEventLog(capacity: 8)
        for action in ["linked", "unlinked", "synced", "settings"] {
            accounts.record(kind: .accountAction, metadata: ["action": action, "cred": "PRIVATE-CREDENTIAL", "uid": "PRIVATE-UID", "name": "PRIVATE-NAME", "url": "https://private.example"])
            check(accounts.events.first?.metadata == ["action": action], "Account events retain only a closed action name")
        }
        accounts.record(kind: .accountAction, metadata: ["action": "PRIVATE-CREDENTIAL"])
        check(accounts.events.first?.metadata.isEmpty == true, "Unrecognized account action strings cannot enter the log")
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
        let actions = SystemEventLog()
        actions.record(kind: .noteAction, metadata: ["action": "formattedText", "text": "private", "path": "/secret", "color": "private"])
        check(actions.events[0].metadata == ["action": "formattedText"], "Note events exclude contents and file references")
        actions.record(kind: .playbackAction, metadata: ["action": "seek", "source": "spotify", "title": "private", "artist": "private", "position": "50"])
        check(actions.events[0].metadata == ["action": "seek", "source": "spotify"], "Playback events exclude listening history and position")
        actions.record(kind: .playbackAction, metadata: ["action": "private", "source": "private.app"])
        check(actions.events[0].metadata.isEmpty, "Playback actions and sources use closed vocabularies")
        actions.record(kind: .noteAction, metadata: ["action": "private"])
        check(actions.events[0].metadata.isEmpty, "Note actions use a closed vocabulary")
        for action in ["drawingEdited", "erased", "brushChanged", "backgroundChanged", "mediaAdded", "mediaRemoved", "cleared"] {
            actions.record(kind: .projectionAction, metadata: ["action": action, "path": "/private/file", "text": "private", "points": "private"])
            check(actions.events[0].metadata == ["action": action] && actions.events[0].category == .display
                  && !actions.events[0].detail.isEmpty, "Projection events keep only the approved action")
        }
        actions.record(kind: .projectionAction, metadata: ["action": "private", "source": "private"])
        check(actions.events[0].metadata.isEmpty, "Projection rejects unknown actions and media metadata")
        for (kind, allowed) in [(SystemEventKind.archiveAction, ["created", "edited", "deleted", "mediaAdded", "mediaRemoved", "categoryCreated", "categoryChanged", "categoryDeleted"]),
                                 (.readerAction, ["imported", "deleted", "bookmarked", "progress", "settings"]),
                                 (.mediaAssemblyAction, ["imported", "edited", "exported"]),
                                 (.calendarAction, ["created", "edited", "deleted"])] {
            for action in allowed {
                actions.record(kind: kind, metadata: ["action": action, "body": "secret", "filename": "diary.txt", "path": "/private/book", "progress": "0.9", "category": "private category", "categoryID": UUID().uuidString])
                check(actions.events[0].metadata == ["action": action] && actions.events[0].category == (kind == .calendarAction ? .work : .files),
                      "Document actions never log text, reading position or source filenames")
                check(!actions.events[0].detail.isEmpty, "Document action has a readable description")
            }
            actions.record(kind: kind, metadata: ["action": "opened", "text": "secret"])
            check(actions.events[0].metadata.isEmpty, "Document events reject navigation and arbitrary content")
        }
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

        let settingsLog = SystemEventLog()
        for (field, values) in [("clockStyle", HUDClockStyle.allCases.map(\.rawValue)),
                                 ("centerLogo", HUDCenterLogo.allCases.map(\.rawValue) + ["customImported"]),
                                 ("alertMetric", HUDChargeMetric.allCases.map(\.rawValue))] {
            for value in values {
                settingsLog.record(kind: .displaySettingsChanged, metadata: ["field": field, "value": value,
                    "revision": UUID().uuidString, "filename": "/private/image.png", "name": "Private Profile"])
                check(settingsLog.events[0].metadata == ["field": field, "value": value]
                    && !settingsLog.events[0].detail.isEmpty && settingsLog.events[0].category == .display,
                      "Settings log retains only a recognized field and its allowed English value")
            }
        }
        for metadata in [["field": "centerLogoRevision", "value": UUID().uuidString],
                         ["field": "centerLogo", "value": "/private/image.png"],
                         ["field": "clockStyle", "value": "customImported"],
                         ["field": "alertMetric", "value": "User secret"], ["value": "cpu"]] {
            settingsLog.record(kind: .displaySettingsChanged, metadata: metadata)
            check(settingsLog.events[0].metadata.isEmpty && settingsLog.events[0].detail.isEmpty,
                  "Unknown keys, cross-field values, paths and content cannot enter settings metadata")
        }
        for target in ["background", "thumbnail", "both"] {
            settingsLog.record(kind: .profileCropChanged, metadata: ["target": target, "zoom": "12.5", "name": "Private"])
            check(settingsLog.events[0].metadata == ["target": target] && !settingsLog.events[0].detail.isEmpty,
                  "Profile crop events identify the committed target without user content or zoom coordinates")
        }
        settingsLog.record(kind: .profileCropChanged, metadata: ["target": "Private Profile", "path": "/private"])
        check(settingsLog.events[0].metadata.isEmpty, "Profile crop target is a closed vocabulary")
        L10n.language = .simplifiedChinese
        settingsLog.record(kind: .displaySettingsChanged, metadata: ["field": "alertMetric", "value": "ram"])
        check(settingsLog.events[0].title == "Display setting changed" && settingsLog.events[0].detail == "Charge metric · RAM",
              "New settings entries remain English under Chinese app localization")
        L10n.language = .english

        let root = FileManager.default.temporaryDirectory.appendingPathComponent("EndfieldEventLogTests-\(UUID().uuidString)", isDirectory: true)
        for style in ["yellow", "green", "player"] {
            settingsLog.record(kind: .mapPinStyleChanged, metadata: ["style": style, "x": "114", "y": "22", "id": UUID().uuidString])
            check(settingsLog.events[0].metadata == ["style": style] && !settingsLog.events[0].detail.isEmpty,
                  "Map style records retain a closed style name without pin identity or location")
        }
        settingsLog.record(kind: .mapPinStyleChanged, metadata: ["style": "/private/map.json"])
        check(settingsLog.events[0].metadata.isEmpty && settingsLog.events[0].detail.isEmpty,
              "Unknown map style strings cannot leak user content")
        settingsLog.record(kind: .mapRecentered, metadata: ["x": "114", "y": "22", "zoom": "3"])
        check(settingsLog.events[0].metadata.isEmpty && settingsLog.events[0].detail.isEmpty,
              "Recentering logs only the action, never camera or location data")
        defer { try? FileManager.default.removeItem(at: root) }
        do {
            let directory = root.appendingPathComponent("persisted", isDirectory: true)
            let file = directory.appendingPathComponent("events.json")
            let persisted = SystemEventLog(directory: directory, capacity: 10)
            persisted.record(kind: .moduleOpened, metadata: ["module": "eventLog"])
            persisted.record(kind: .workCompleted, metadata: ["kind": "stopwatch", "seconds": "123"])
            persisted.record(kind: .displaySettingsChanged, metadata: ["field": "centerLogo", "value": "customImported"])
            persisted.record(kind: .profileCropChanged, metadata: ["target": "both"])
            persisted.record(kind: .mapPinStyleChanged, metadata: ["style": "player"])
            persisted.record(kind: .mapRecentered)
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
