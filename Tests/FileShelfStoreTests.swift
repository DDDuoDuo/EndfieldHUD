import Foundation
import Darwin

enum FileShelfStoreTests {
    static func run() -> Int {
        var count = 0
        func check(_ condition: Bool, _ message: String, file: StaticString = #file, line: UInt = #line) {
            count += 1
            if !condition { fatalError(message, file: file, line: line) }
        }
        func rejected(_ action: () throws -> Void) -> Bool {
            do { try action(); return false } catch { return true }
        }
        let fm = FileManager.default
        let root = fm.temporaryDirectory.appendingPathComponent("FileShelfStoreTests-\(UUID().uuidString)", isDirectory: true)
        defer { try? fm.removeItem(at: root) }
        do {
            let originals = root.appendingPathComponent("originals", isDirectory: true)
            try fm.createDirectory(at: originals, withIntermediateDirectories: true)
            let location = root.appendingPathComponent("shelf", isDirectory: true)
            let store = try FileShelfStore(directory: location)
            check(store.items.isEmpty, "A new file shelf starts empty")
            check(FileShelfStore.applicationDirectory() == FileShelfStore.applicationDirectory(),
                  "The shelf directory is stable across HUD reopening within one process")
            let names = ["document.txt", "图像.png", "paper.pdf", "movie.mov", "archive.zip", "other.custom"]
            let urls = names.map { originals.appendingPathComponent($0) }
            let originalBytes = Data("Original bytes must not be changed or copied into the shelf.\0\n".utf8)
            for url in urls { try originalBytes.write(to: url) }
            let folder = originals.appendingPathComponent("Folder", isDirectory: true)
            try fm.createDirectory(at: folder, withIntermediateDirectories: true)
            let nested = folder.appendingPathComponent("nested.txt")
            try originalBytes.write(to: nested)
            let package = originals.appendingPathComponent("Example.app", isDirectory: true)
            try fm.createDirectory(at: package, withIntermediateDirectories: true)
            let huge = originals.appendingPathComponent("huge-video.mov")
            check(fm.createFile(atPath: huge.path, contents: nil), "Sparse test video is created")
            let handle = try FileHandle(forWritingTo: huge)
            handle.truncateFile(atOffset: 64 * 1024 * 1024 * 1024)
            handle.closeFile()
            let initialURLs = urls + [folder, package, huge]
            check(try store.add(urls: initialURLs) == initialURLs.count,
                  "Files, folders, images, PDFs, videos, archives, packages and unknown Finder types are accepted")
            check(store.items.map(\.name) == names + ["Folder", "Example.app", "huge-video.mov"],
                  "Shelf insertion order and Unicode filenames are preserved")
            check(store.items.allSatisfy { !$0.bookmark.isEmpty && !$0.typeDescription.isEmpty && $0.availabilityError == nil },
                  "Every new shelf card has a bookmark, type description and available status")
            check(store.items[0].byteCount == Int64(originalBytes.count) && store.items[0].sizeLabel != nil,
                  "File cards report metadata size without reading file contents")
            check(store.items[6].isDirectory && store.items[6].byteCount == nil && store.items[6].sizeLabel == nil,
                  "Folders do not trigger recursive size enumeration")
            check(store.items[7].isDirectory && store.items[7].byteCount == nil, "Finder packages remain directory references")
            check(store.items.last?.byteCount == 64 * 1024 * 1024 * 1024,
                  "A sparse 64 GB video is represented by its metadata only")
            let filesInShelf = try fm.contentsOfDirectory(atPath: location.path)
            check(filesInShelf == ["shelf.json"], "The shelf owns only its small metadata archive, with no copied originals")
            let archiveURL = location.appendingPathComponent("shelf.json")
            let archiveSize = try archiveURL.resourceValues(forKeys: [.fileSizeKey]).fileSize ?? 0
            check(archiveSize < 100_000, "A shelf containing a 64 GB file persists in less than 100 KB")
            let beforeDuplicate = store.items
            let beforeDuplicateBytes = try Data(contentsOf: archiveURL)
            check(try store.add(urls: [urls[0], urls[0], folder]) == 0, "Repeated drops deduplicate file identity")
            check(try store.items == beforeDuplicate && Data(contentsOf: archiveURL) == beforeDuplicateBytes,
                  "Dropping only duplicates causes no metadata write")
            let hardLink = originals.appendingPathComponent("same-file.txt")
            try fm.linkItem(at: urls[0], to: hardLink)
            check(try store.add(urls: [hardLink]) == 0, "Hard links to the same item cannot create duplicate shelf references")

            let reopened = try FileShelfStore(directory: location)
            check(reopened.items == store.items, "All persistent fields survive a new store instance")
            try reopened.refresh()
            check(reopened.items.count == initialURLs.count && reopened.items.allSatisfy { $0.availabilityError == nil },
                  "Bookmarks resolve after reopening without presenting permission UI")
            for item in reopened.items {
                let access = try reopened.access(id: item.id)
                check(access.url.isFileURL && fm.fileExists(atPath: access.url.path), "Scoped or regular access returns the existing Finder item")
                access.close(); access.close()
            }
            check(rejected { _ = try store.access(id: UUID()) }, "Access to a deleted shelf identity returns a meaningful failure")

            let movedDirectory = root.appendingPathComponent("moved", isDirectory: true)
            try fm.createDirectory(at: movedDirectory, withIntermediateDirectories: true)
            let movedURL = movedDirectory.appendingPathComponent("renamed-document.txt")
            let movedID = reopened.items[0].id
            let movedCreation = reopened.items[0].createdAt
            try fm.moveItem(at: urls[0], to: movedURL)
            try reopened.refresh()
            let moved = reopened.items.first { $0.id == movedID }
            check(moved?.availabilityError == nil && moved?.name == "renamed-document.txt"
                  && moved.map { canonical(URL(fileURLWithPath: $0.lastKnownPath)) } == canonical(movedURL) && moved?.createdAt == movedCreation,
                  "Bookmarks follow a renamed and moved original while retaining card identity and creation time")
            let afterMove = try FileShelfStore(directory: location)
            let movedAccess = try afterMove.access(id: movedID)
            check(canonical(movedAccess.url) == canonical(movedURL), "A refreshed moved bookmark persists across another launch")
            movedAccess.close()
            check(try reopened.add(urls: [movedURL]) == 0, "A moved original still deduplicates by identity")

            let deletedID = reopened.items[1].id
            try fm.removeItem(at: urls[1])
            try reopened.refresh()
            check(reopened.items.count == initialURLs.count && reopened.items.first { $0.id == deletedID }?.availabilityError != nil,
                  "Missing items remain on the shelf with their last known metadata")
            check(rejected { _ = try reopened.access(id: deletedID) }, "Missing references cannot produce a bogus drag URL")
            let missingReload = try FileShelfStore(directory: location)
            check(missingReload.items.first { $0.id == deletedID }?.availabilityError == nil,
                  "Runtime availability errors are not persisted as stale card state")
            try missingReload.refresh()
            check(missingReload.items.first { $0.id == deletedID }?.availabilityError != nil,
                  "Missing-item availability is recomputed when the shelf activates")

            // Keep the old inode alive outside the bookmark path to ensure this
            // replacement receives a distinct identity even on APFS inode reuse.
            let replacementID = reopened.items[2].id
            let hiddenOriginal = originals.appendingPathComponent("old-paper.pdf")
            try fm.linkItem(at: urls[2], to: hiddenOriginal)
            try fm.removeItem(at: urls[2])
            try Data("Different file".utf8).write(to: urls[2])
            do {
                let access = try reopened.access(id: replacementID)
                check(canonical(access.url) != canonical(urls[2]), "Bookmark relocation never points at an unrelated replacement")
                access.close()
            } catch { check(true, "Unrelated path replacements are rejected when the original cannot be resolved") }

            let beforeInvalid = reopened.items
            let newFile = originals.appendingPathComponent("not-committed.txt")
            try originalBytes.write(to: newFile)
            check(rejected { _ = try reopened.add(urls: [newFile, URL(string: "https://example.com/file")!]) },
                  "A mixed invalid drop is rejected before committing any records")
            check(reopened.items == beforeInvalid, "A failed batch import leaves the in-memory shelf unchanged")
            check(rejected { _ = try reopened.add(urls: [originals.appendingPathComponent("missing-file")]) },
                  "Nonexistent paths cannot enter the shelf")

            let links = root.appendingPathComponent("link-shelf", isDirectory: true)
            let linkStore = try FileShelfStore(directory: links)
            let symbolicLink = originals.appendingPathComponent("Symbolic link.txt")
            try fm.createSymbolicLink(at: symbolicLink, withDestinationURL: newFile)
            check(try linkStore.add(urls: [symbolicLink]) == 1, "Normal symbolic links can be placed on the shelf")
            let linkAccess = try linkStore.access(id: linkStore.items[0].id)
            check(try linkAccess.url.resourceValues(forKeys: [.isSymbolicLinkKey]).isSymbolicLink == true,
                  "Dragging a symbolic link preserves the link item instead of substituting its target")
            linkAccess.close()
            let alias = originals.appendingPathComponent("Finder alias")
            let aliasData = try newFile.bookmarkData(options: .suitableForBookmarkFile, includingResourceValuesForKeys: nil, relativeTo: nil)
            try URL.writeBookmarkData(aliasData, to: alias)
            check(try linkStore.add(urls: [alias]) == 1, "Finder alias files can be placed on the shelf")
            let aliasAccess = try linkStore.access(id: linkStore.items[1].id)
            check(try aliasAccess.url.resourceValues(forKeys: [.isAliasFileKey]).isAliasFile == true,
                  "Dragging a Finder alias preserves the alias file itself")
            aliasAccess.close()
            try linkStore.refresh()
            check(linkStore.items.allSatisfy { $0.availabilityError == nil }, "Symlinks and aliases remain accessible after refresh")
            let fifo = originals.appendingPathComponent("pipe")
            check(mkfifo(fifo.path, 0o600) == 0, "A named pipe fixture is created without opening it")
            check(rejected { _ = try linkStore.add(urls: [fifo]) }, "Special files such as FIFO pipes are rejected without reading contents")

            let readonly = root.appendingPathComponent("readonly", isDirectory: true)
            let readonlyStore = try FileShelfStore(directory: readonly)
            try readonlyStore.add(urls: [newFile])
            let readonlyBefore = readonlyStore.items
            let readonlyBytes = try Data(contentsOf: readonly.appendingPathComponent("shelf.json"))
            try fm.setAttributes([.posixPermissions: 0o500], ofItemAtPath: readonly.path)
            let writeRejected = rejected { try readonlyStore.clear() }
            try fm.setAttributes([.posixPermissions: 0o700], ofItemAtPath: readonly.path)
            check(writeRejected, "Read-only persistence failures propagate to the caller")
            check(try readonlyStore.items == readonlyBefore && Data(contentsOf: readonly.appendingPathComponent("shelf.json")) == readonlyBytes,
                  "Failed commits roll back memory and preserve the existing archive")

            let corrupt = root.appendingPathComponent("corrupt", isDirectory: true)
            try fm.createDirectory(at: corrupt, withIntermediateDirectories: true)
            let corruptURL = corrupt.appendingPathComponent("shelf.json")
            let corruptBytes = Data("broken JSON — preserve these bytes".utf8)
            try corruptBytes.write(to: corruptURL)
            check(rejected { _ = try FileShelfStore(directory: corrupt) }, "Corrupt archives are not silently replaced")
            check(try Data(contentsOf: corruptURL) == corruptBytes, "Corrupt archive bytes are preserved")
            let futureBytes = Data("{\"version\":99,\"unknown_future_field\":true}".utf8)
            try futureBytes.write(to: corruptURL)
            check(rejected { _ = try FileShelfStore(directory: corrupt) }, "Unknown future archive versions are refused before writing")
            check(try Data(contentsOf: corruptURL) == futureBytes, "Future archive bytes are preserved")

            let external = root.appendingPathComponent("external", isDirectory: true)
            let externalStore = try FileShelfStore(directory: external)
            try externalStore.add(urls: [newFile])
            let externalItems = externalStore.items
            try corruptBytes.write(to: external.appendingPathComponent("shelf.json"))
            check(rejected { try externalStore.clear() }, "An existing store cannot overwrite externally replaced or corrupt data")
            check(try externalStore.items == externalItems && Data(contentsOf: external.appendingPathComponent("shelf.json")) == corruptBytes,
                  "External-data conflict preserves both the last in-memory snapshot and on-disk bytes")

            let cleanup = root.appendingPathComponent("cleanup", isDirectory: true)
            let cleanupStore = try FileShelfStore(directory: cleanup)
            try cleanupStore.add(urls: [movedURL, folder, huge])
            try cleanupStore.remove(id: cleanupStore.items[0].id)
            check(try Data(contentsOf: movedURL) == originalBytes, "Remove from shelf never deletes or changes the original")
            let beforeNoop = cleanupStore.items
            try cleanupStore.remove(id: UUID())
            check(cleanupStore.items == beforeNoop, "Removing an unknown identity is harmless")
            try cleanupStore.clear()
            check(try cleanupStore.items.isEmpty && FileShelfStore(directory: cleanup).items.isEmpty,
                  "Clear all persists an empty shelf across launch")
            check(try Data(contentsOf: nested) == originalBytes && fm.fileExists(atPath: huge.path),
                  "Clear all leaves folders, contents and huge original files untouched")
            check(try huge.resourceValues(forKeys: [.fileSizeKey]).fileSize == 64 * 1024 * 1024 * 1024,
                  "No shelf operation modifies the referenced large file")
        } catch { fatalError("File shelf persistence test failed: \(error)") }
        return count
    }

    private static func canonical(_ url: URL) -> String {
        guard let path = realpath(url.path, nil) else { return url.path }
        defer { free(path) }
        return String(cString: path)
    }
}
