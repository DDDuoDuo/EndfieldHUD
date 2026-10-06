import AppKit

enum AppShortcutStoreTests {
    static func run() -> Int {
        var count = 0
        func check(_ condition: Bool, _ message: String, file: StaticString = #file, line: UInt = #line) {
            count += 1
            if !condition { fatalError(message, file: file, line: line) }
        }
        func rejected(_ operation: () throws -> Void) -> Bool {
            do { try operation(); return false } catch { return true }
        }
        let fm = FileManager.default
        let root = fm.temporaryDirectory.appendingPathComponent("AppShortcutStoreTests-\(UUID().uuidString)", isDirectory: true)
        defer { try? fm.removeItem(at: root) }
        func fixture(_ name: String, identifier: String? = "test.endfield.fixture", packageType: String = "APPL",
                     executable: String = "Fixture", executableMode: Int = 0o700, flat: Bool = false) throws -> URL {
            let url = root.appendingPathComponent(name, isDirectory: true)
            let binaryDirectory = flat ? url : url.appendingPathComponent("Contents/MacOS", isDirectory: true)
            try fm.createDirectory(at: binaryDirectory, withIntermediateDirectories: true)
            var info: [String: Any] = ["CFBundlePackageType": packageType, "CFBundleExecutable": executable,
                                       "CFBundleName": "Internal fixture", "CFBundleDisplayName": "Fixture 应用"]
            if let identifier { info["CFBundleIdentifier"] = identifier }
            try PropertyListSerialization.data(fromPropertyList: info, format: .xml, options: 0)
                .write(to: url.appendingPathComponent(flat ? "Info.plist" : "Contents/Info.plist"))
            let binary = binaryDirectory.appendingPathComponent("Fixture")
            try Data("#!/bin/sh\nexit 0\n".utf8).write(to: binary)
            try fm.setAttributes([.posixPermissions: executableMode], ofItemAtPath: binary.path)
            return url
        }
        func wrappedFixture(_ name: String, packageType: String = "APPL", executable: String = "Fixture",
                            executableMode: Int = 0o700) throws -> URL {
            _ = try fixture("\(name)/Wrapper/Cloud.app", packageType: packageType, executable: executable,
                            executableMode: executableMode, flat: true)
            let url = root.appendingPathComponent(name, isDirectory: true)
            try fm.createSymbolicLink(atPath: url.appendingPathComponent("WrappedBundle").path,
                                      withDestinationPath: "Wrapper/Cloud.app")
            return url
        }
        do {
            let app = try fixture("Example.app")
            let directory = root.appendingPathComponent("store", isDirectory: true)
            let store = try AppShortcutStore(directory: directory)
            check(store.items.isEmpty, "A new app library is empty")
            check(AppShortcutStore.applicationDirectory() == AppShortcutStore.applicationDirectory(), "The app directory is stable within a process")
            check(AppShortcutIcon.allCases.count == 14 && Set(AppShortcutIcon.allCases.map(\.rawValue)).count == 14,
                  "Original icons and thirteen distinct persistent presets are available")
            check(AppShortcutIcon.allCases.dropLast().map(\.rawValue) == ["original", "bolt", "star", "terminal", "globe", "folder", "music", "play", "brush", "code", "game", "camera", "grid"]
                  && AppShortcutIcon.allCases.last == .textBubble, "The text bubble appends a stable case without changing existing saved preset identities")
            check(try JSONDecoder().decode(AppShortcutIcon.self, from: JSONEncoder().encode(AppShortcutIcon.textBubble)) == .textBubble,
                  "The text bubble preset round-trips through the saved Codable representation")
            let oldLanguage = L10n.language
            L10n.language = .english
            let englishTitles = AppShortcutIcon.allCases.map(\.title)
            L10n.language = .simplifiedChinese
            check(zip(englishTitles, AppShortcutIcon.allCases.map(\.title)).allSatisfy { !$0.0.isEmpty && !$0.1.isEmpty && $0.0 != $0.1 },
                  "All preset choices have English and Chinese labels")
            L10n.language = oldLanguage
            let candidate = try store.inspect(url: app)
            check(candidate.name == "Fixture 应用" && candidate.bundleIdentifier == "test.endfield.fixture", "Inspection reads the app display name and bundle identity")
            check(candidate.icon != nil, "Inspection retrieves a native app icon without copying it to storage")
            let first = try store.save(candidate: candidate, name: "  My 应用  ", iconPreset: .original)
            check(first.name == "My 应用" && first.originalName == candidate.name && first.iconPreset == .original,
                  "The shortcut retains its original name and saves the trimmed custom name")
            check(first.createdAt.timeIntervalSinceNow > -10, "A newly saved shortcut records its creation time")
            check(try store.resolvedURL(for: first.id).resolvingSymlinksInPath() == app.resolvingSymlinksInPath(), "Bookmarks resolve the chosen application")
            check(store.icon(for: first.id) === store.icon(for: first.id), "Repeated displays reuse an in-memory icon")
            let archive = directory.appendingPathComponent("shortcuts.json")
            check(try fm.contentsOfDirectory(atPath: directory.path) == ["shortcuts.json"], "Persistence contains only bookmark metadata, with no copied application or icon")
            check(try Data(contentsOf: archive).count < 20_000, "A shortcut archive stays small")
            let reopened = try AppShortcutStore(directory: directory)
            check(reopened.items == store.items, "All shortcut fields persist across app launches")
            let beforeDuplicate = try Data(contentsOf: archive)
            check(rejected { _ = try store.save(candidate: candidate, name: "Duplicate", iconPreset: .star) }, "The same application cannot be added twice")
            check(try Data(contentsOf: archive) == beforeDuplicate && store.items == reopened.items, "Duplicate rejection does not write or mutate the library")
            let alias = root.appendingPathComponent("Linked.app")
            try fm.createSymbolicLink(at: alias, withDestinationURL: app)
            let aliasCandidate = try store.inspect(url: alias)
            check(rejected { _ = try store.save(candidate: aliasCandidate, name: "Alias", iconPreset: .star) }, "Symlink paths to the same app do not create duplicate shortcuts")
            let renamed = try store.save(candidate: candidate, name: "Renamed", iconPreset: .terminal, editingID: first.id)
            check(renamed.id == first.id && renamed.createdAt == first.createdAt && renamed.iconPreset == .terminal && renamed.name == "Renamed",
                  "Editing changes the label and preset while preserving identity and creation time")
            check(store.items.count == 1, "An edited shortcut replaces its existing record")
            check(rejected { _ = try store.save(candidate: candidate, name: "Missing", iconPreset: .original, editingID: UUID()) }, "An unknown edit ID cannot create an accidental new shortcut")
            for invalidName in ["", " \n ", "A\nB", "A\0B", String(repeating: "a", count: 129)] {
                check(rejected { _ = try store.save(candidate: candidate, name: invalidName, iconPreset: .original, editingID: first.id) }, "Empty, multiline, control and overlong names are rejected")
            }
            let secondURL = try fixture("Second.app", identifier: "test.endfield.second")
            let secondCandidate = try store.inspect(url: secondURL)
            let second = try store.save(candidate: secondCandidate, name: secondCandidate.name, iconPreset: .textBubble)
            check(store.items.map(\.id) == [first.id, second.id], "Shortcut creation preserves library ordering")
            check(try AppShortcutStore(directory: directory).items.last?.iconPreset == .textBubble,
                  "A text bubble shortcut reloads its selected icon from the persisted archive")
            check(rejected { _ = try store.save(candidate: secondCandidate, name: "Duplicate edit", iconPreset: .star, editingID: first.id) }, "Editing cannot duplicate another saved application")
            let sameBundleURL = try fixture("Other installation.app")
            let sameBundle = try store.inspect(url: sameBundleURL)
            let separate = try store.save(candidate: sameBundle, name: "Other installation", iconPreset: .code)
            check(separate.id != first.id && store.items.count == 3, "Separate installations sharing a bundle ID may be saved explicitly")
            let movingDirectory = root.appendingPathComponent("relocated", isDirectory: true)
            try fm.createDirectory(at: movingDirectory, withIntermediateDirectories: true)
            let movedURL = movingDirectory.appendingPathComponent("Renamed.app", isDirectory: true)
            try fm.moveItem(at: app, to: movedURL)
            check(try store.resolvedURL(for: first.id).resolvingSymlinksInPath() == movedURL.resolvingSymlinksInPath(), "Bookmarks follow moved and renamed applications")
            let movedReload = try AppShortcutStore(directory: directory)
            check(try movedReload.resolvedURL(for: first.id).resolvingSymlinksInPath() == movedURL.resolvingSymlinksInPath(), "Moved app locations persist after another launch")
            check(movedReload.items.first?.name == "Renamed" && movedReload.items.first?.createdAt == first.createdAt,
                  "Moving an application preserves its custom name and creation time")
            let movedCandidate = try store.inspect(url: movedURL)
            check(rejected { _ = try store.save(candidate: movedCandidate, name: "Again", iconPreset: .bolt) }, "Moved applications still deduplicate")
            try fm.removeItem(at: secondURL)
            check(rejected { _ = try store.resolvedURL(for: second.id) }, "Missing applications report failure instead of opening another installation")
            check(store.items.contains { $0.id == second.id }, "Missing shortcuts remain removable in the library")
            check(store.icon(for: UUID()) == nil, "An unknown shortcut has no cached icon")
            try store.remove(id: second.id)
            check(!store.items.contains { $0.id == second.id }, "Removing a missing application reference succeeds")
            check(fm.fileExists(atPath: sameBundleURL.path), "Removing a shortcut never deletes the application")
            let noOpBytes = try Data(contentsOf: archive)
            try store.remove(id: UUID())
            check(try Data(contentsOf: archive) == noOpBytes, "Removing an unknown shortcut does not write the archive")
            check(rejected { _ = try store.inspect(url: URL(string: "https://example.com/App.app")!) }, "Remote URLs cannot become app shortcuts")
            check(rejected { _ = try store.inspect(url: root) }, "Ordinary folders are rejected")
            let fake = root.appendingPathComponent("Fake.app")
            try Data("not an application".utf8).write(to: fake)
            check(rejected { _ = try store.inspect(url: fake) }, "A file with an .app suffix is rejected")
            let wrongPackage = try fixture("Bundle.app", packageType: "BNDL")
            check(rejected { _ = try store.inspect(url: wrongPackage) }, "Non-application bundles are rejected")
            let noExecutable = try fixture("NonExecutable.app", executableMode: 0o600)
            check(rejected { _ = try store.inspect(url: noExecutable) }, "Applications need an executable file")
            let traversingExecutable = try fixture("Traversal.app", executable: "../../../bin/sh")
            check(rejected { _ = try store.inspect(url: traversingExecutable) }, "Executable metadata cannot escape the expected bundle layout")
            let missingExecutable = try fixture("MissingExecutable.app", executable: "Gone")
            check(rejected { _ = try store.inspect(url: missingExecutable) }, "Missing executables are rejected")

            let alternateDirectory = root.appendingPathComponent("alternate-store", isDirectory: true)
            let alternateStore = try AppShortcutStore(directory: alternateDirectory)
            let flat = try fixture("Flat.app", flat: true)
            let flatCandidate = try alternateStore.inspect(url: flat)
            check(flatCandidate.name == "Fixture 应用" && flatCandidate.bundleIdentifier == candidate.bundleIdentifier,
                  "Flat application bundles read their root metadata and executable")
            let flatItem = try alternateStore.save(candidate: flatCandidate, name: "Flat app", iconPreset: .original)
            check(try alternateStore.resolvedURL(for: flatItem.id) == flat.resolvingSymlinksInPath(),
                  "Flat application bookmarks resolve to the selected application")
            let wrapped = try wrappedFixture("云·终末地.app")
            let wrappedCandidate = try alternateStore.inspect(url: wrapped)
            check(wrappedCandidate.url == wrapped.resolvingSymlinksInPath() && wrappedCandidate.name == "Fixture 应用"
                  && wrappedCandidate.bundleIdentifier == candidate.bundleIdentifier && wrappedCandidate.icon != nil,
                  "iOS-on-Mac wrappers expose the inner app metadata while retaining the outer application URL and icon")
            let wrappedItem = try alternateStore.save(candidate: wrappedCandidate, name: "Cloud app", iconPreset: .original)
            let alternateReload = try AppShortcutStore(directory: alternateDirectory)
            check(alternateReload.items == alternateStore.items, "Flat and wrapped applications persist through the existing archive format")
            check(try alternateReload.resolvedURL(for: wrappedItem.id) == wrapped.resolvingSymlinksInPath(),
                  "Wrapped app bookmarks give LaunchServices the outer application, never the embedded executable or bundle")
            let wrappedAlias = root.appendingPathComponent("Cloud link.app")
            try fm.createSymbolicLink(at: wrappedAlias, withDestinationURL: wrapped)
            let wrappedAliasCandidate = try alternateStore.inspect(url: wrappedAlias)
            check(rejected { _ = try alternateStore.save(candidate: wrappedAliasCandidate, name: "Again", iconPreset: .star) },
                  "Aliases to the same wrapped installation are deduplicated")
            let movedWrapped = root.appendingPathComponent("Cloud renamed.app", isDirectory: true)
            try fm.moveItem(at: wrapped, to: movedWrapped)
            check(try alternateStore.resolvedURL(for: wrappedItem.id) == movedWrapped.resolvingSymlinksInPath(),
                  "Bookmarks follow moved wrappers and resolve their relative WrappedBundle link")
            let movedWrappedCandidate = try alternateStore.inspect(url: movedWrapped)
            let wrappedInfoURL = movedWrapped.appendingPathComponent("Wrapper/Cloud.app/Info.plist")
            var wrappedInfo = try PropertyListSerialization.propertyList(from: Data(contentsOf: wrappedInfoURL), options: [], format: nil) as! [String: Any]
            wrappedInfo["CFBundleIdentifier"] = "test.endfield.changed-wrapper"
            try PropertyListSerialization.data(fromPropertyList: wrappedInfo, format: .xml, options: 0).write(to: wrappedInfoURL)
            check(rejected { _ = try alternateStore.save(candidate: movedWrappedCandidate, name: "Stale", iconPreset: .original, editingID: wrappedItem.id) },
                  "A stale wrapped-app draft cannot save changed embedded application identity")
            check(rejected { _ = try alternateStore.resolvedURL(for: wrappedItem.id) },
                  "Wrapped bookmarks refuse a replacement inner application")
            for (label, type, executable, mode) in [("WrongType", "BNDL", "Fixture", 0o700),
                                                   ("NoPermission", "APPL", "Fixture", 0o600),
                                                   ("Traversal", "APPL", "../Fixture", 0o700),
                                                   ("Missing", "APPL", "Gone", 0o700)] {
                let invalidFlat = try fixture("Flat\(label).app", packageType: type, executable: executable, executableMode: mode, flat: true)
                let invalidWrapped = try wrappedFixture("Wrapped\(label).app", packageType: type, executable: executable, executableMode: mode)
                check(rejected { _ = try alternateStore.inspect(url: invalidFlat) }, "Flat layouts retain package and executable validation: \(label)")
                check(rejected { _ = try alternateStore.inspect(url: invalidWrapped) }, "Wrapped layouts retain package and executable validation: \(label)")
            }
            let escapingWrapper = try wrappedFixture("EscapingWrapper.app")
            try fm.removeItem(at: escapingWrapper.appendingPathComponent("WrappedBundle"))
            try fm.createSymbolicLink(at: escapingWrapper.appendingPathComponent("WrappedBundle"), withDestinationURL: flat)
            check(rejected { _ = try alternateStore.inspect(url: escapingWrapper) }, "WrappedBundle cannot substitute an app outside the selected package")
            let escapingDirectory = root.appendingPathComponent("EscapingDirectory.app", isDirectory: true)
            try fm.createDirectory(at: escapingDirectory, withIntermediateDirectories: true)
            try fm.createSymbolicLink(at: escapingDirectory.appendingPathComponent("Wrapper"), withDestinationURL: root)
            try fm.createSymbolicLink(atPath: escapingDirectory.appendingPathComponent("WrappedBundle").path, withDestinationPath: "Wrapper/Flat.app")
            check(rejected { _ = try alternateStore.inspect(url: escapingDirectory) }, "A symlinked Wrapper directory cannot escape the selected package")
            let noLink = try wrappedFixture("MissingWrappedLink.app")
            try fm.removeItem(at: noLink.appendingPathComponent("WrappedBundle"))
            check(rejected { _ = try alternateStore.inspect(url: noLink) }, "Missing wrapper metadata never triggers a search for an embedded or installed substitute")
            let brokenLink = try wrappedFixture("BrokenWrappedLink.app")
            try fm.removeItem(at: brokenLink.appendingPathComponent("Wrapper/Cloud.app"))
            check(rejected { _ = try alternateStore.inspect(url: brokenLink) }, "A broken WrappedBundle link is rejected")
            let brokenNative = try fixture("BrokenNative.app")
            try fm.moveItem(at: brokenNative.appendingPathComponent("Contents/Info.plist"), to: brokenNative.appendingPathComponent("Info.plist"))
            try fm.moveItem(at: brokenNative.appendingPathComponent("Contents/MacOS/Fixture"), to: brokenNative.appendingPathComponent("Fixture"))
            check(rejected { _ = try alternateStore.inspect(url: brokenNative) }, "A broken native layout cannot fall through to unrelated root metadata")
            let staleDraft = try store.inspect(url: sameBundleURL)
            var info = try PropertyListSerialization.propertyList(from: Data(contentsOf: sameBundleURL.appendingPathComponent("Contents/Info.plist")), options: [], format: nil) as! [String: Any]
            info["CFBundleIdentifier"] = "test.endfield.replacement"
            try PropertyListSerialization.data(fromPropertyList: info, format: .xml, options: 0)
                .write(to: sameBundleURL.appendingPathComponent("Contents/Info.plist"))
            check(rejected { _ = try store.save(candidate: staleDraft, name: "Old draft", iconPreset: .star, editingID: separate.id) }, "Saving a stale draft detects a replaced application")
            check(rejected { _ = try store.resolvedURL(for: separate.id) }, "Resolving a shortcut refuses a different bundle identity")

            let corruptDirectory = root.appendingPathComponent("corrupt", isDirectory: true)
            try fm.createDirectory(at: corruptDirectory, withIntermediateDirectories: true)
            let corruptURL = corruptDirectory.appendingPathComponent("shortcuts.json")
            for bad in [Data("not JSON".utf8), Data("{\"version\":99}".utf8), Data("{\"version\":1,\"items\":[{}]}".utf8)] {
                try bad.write(to: corruptURL)
                check(rejected { _ = try AppShortcutStore(directory: corruptDirectory) }, "Corrupt and unsupported archives fail explicitly")
                check(try Data(contentsOf: corruptURL) == bad, "Unreadable original data is preserved exactly")
            }
            let beforeConflict = store.items
            let externalBytes = Data("externally changed data".utf8)
            try externalBytes.write(to: archive)
            check(rejected { try store.remove(id: first.id) }, "External edits cannot be overwritten by a stale store")
            check(try store.items == beforeConflict && Data(contentsOf: archive) == externalBytes, "Conflict rejection preserves both memory and external bytes")
            let readOnlyDirectory = root.appendingPathComponent("read-only", isDirectory: true)
            let readOnlyStore = try AppShortcutStore(directory: readOnlyDirectory)
            let readOnlyItem = try readOnlyStore.save(candidate: movedCandidate, name: "Keep", iconPreset: .music)
            let readOnlyArchive = readOnlyDirectory.appendingPathComponent("shortcuts.json")
            let readOnlyBytes = try Data(contentsOf: readOnlyArchive)
            try fm.setAttributes([.posixPermissions: 0o500], ofItemAtPath: readOnlyDirectory.path)
            let failedWrite = rejected { try readOnlyStore.remove(id: readOnlyItem.id) }
            try fm.setAttributes([.posixPermissions: 0o700], ofItemAtPath: readOnlyDirectory.path)
            check(failedWrite, "Persistence errors reach the caller")
            check(try readOnlyStore.items == [readOnlyItem] && Data(contentsOf: readOnlyArchive) == readOnlyBytes,
                  "An unsuccessful atomic write rolls back memory and preserves the original archive")
        } catch { fatalError("App shortcut fixture failed: \(error)") }
        return count
    }
}
