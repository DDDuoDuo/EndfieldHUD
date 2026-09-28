import Foundation
import Security
import Darwin

struct ShelfItem: Identifiable, Equatable {
    var id: UUID
    var bookmark: Data
    var isSecurityScoped: Bool
    var lastKnownPath: String
    var name: String
    var typeDescription: String
    var byteCount: Int64?
    var isDirectory: Bool
    var createdAt: Date
    // Availability belongs to the current session, not the persisted record.
    var availabilityError: String?
    fileprivate var identity: ShelfFileIdentity?

    init(id: UUID = UUID(), bookmark: Data, isSecurityScoped: Bool, lastKnownPath: String,
         name: String, typeDescription: String, byteCount: Int64?, isDirectory: Bool,
         createdAt: Date = Date(), availabilityError: String? = nil) {
        self.id = id; self.bookmark = bookmark; self.isSecurityScoped = isSecurityScoped
        self.lastKnownPath = lastKnownPath; self.name = name; self.typeDescription = typeDescription
        self.byteCount = byteCount; self.isDirectory = isDirectory; self.createdAt = createdAt
        self.availabilityError = availabilityError
    }

    var sizeLabel: String? {
        guard !isDirectory, let byteCount else { return nil }
        return ByteCountFormatter.string(fromByteCount: byteCount, countStyle: .file)
    }
}

/// A bookmark can fall back to a pathname after its original item disappears.
/// Retain filesystem identity so a replacement at that path is never mistaken
/// for the user's original file. This does not inspect any file contents.
fileprivate struct ShelfFileIdentity: Codable, Equatable {
    var inode: UInt64
    var volumeUUID: String?
    var device: Int32

    func matches(_ other: ShelfFileIdentity) -> Bool {
        guard inode == other.inode else { return false }
        if let volumeUUID, let otherVolume = other.volumeUUID { return volumeUUID == otherVolume }
        return device == other.device
    }
}

enum FileShelfStoreError: LocalizedError {
    case invalidRecord
    case newerVersion
    case changedOnDisk
    case invalidFile
    case missingItem
    case replacedItem
    case unavailable(String)
    case persistence(String)

    var errorDescription: String? {
        switch self {
        case .invalidRecord:
            return L10n.text("The saved file shelf could not be read. Its original data has been preserved.", "无法读取暂存架数据，原始数据已保留。")
        case .newerVersion:
            return L10n.text("This file shelf was saved by a newer version of EndfieldCharge.", "此暂存架由较新版本的 EndfieldCharge 保存。")
        case .changedOnDisk:
            return L10n.text("The saved shelf changed outside this window. Restart EndfieldCharge before making changes.", "暂存架数据已在外部更改，请重新启动 EndfieldCharge 后再编辑。")
        case .invalidFile:
            return L10n.text("Choose a file or folder that is available on this Mac.", "请选择此 Mac 上可访问的文件或文件夹。")
        case .missingItem:
            return L10n.text("This item is no longer on the shelf.", "此项目已不在暂存架中。")
        case .replacedItem:
            return L10n.text("The original item is unavailable. A different item now occupies its old location.", "原始项目不可用，其他项目已占用了原来的位置。")
        case .unavailable(let detail):
            return L10n.text("This item is unavailable: ", "此项目不可用：") + detail
        case .persistence(let detail):
            return L10n.text("The file shelf could not be saved or opened: ", "无法保存或打开暂存架：") + detail
        }
    }
}

/// Keep this object alive while a drag, Quick Look panel, or other consumer uses
/// its URL. close() is idempotent; deinit balances any successful scoped start.
final class ShelfFileAccess {
    let url: URL
    private var scoped: Bool

    fileprivate init(url: URL, securityScoped: Bool) {
        self.url = url
        scoped = securityScoped && url.startAccessingSecurityScopedResource()
    }

    func close() {
        guard scoped else { return }
        scoped = false
        url.stopAccessingSecurityScopedResource()
    }

    deinit { close() }
}

/// Stores small bookmarks and metadata only. Operations never copy, move,
/// modify, recursively enumerate, or delete the referenced Finder items.
/// The owning HUD calls refresh on activation; there are no polling timers.
final class FileShelfStore {
    private(set) var items: [ShelfItem] = []
    private let fileURL: URL
    private var persistedData: Data?
    private let fileManager = FileManager.default

    private struct Archive: Codable {
        let version: Int
        var items: [Record]
    }
    private struct Version: Decodable { let version: Int }
    private struct Record: Codable {
        var id: UUID
        var bookmark: Data
        var isSecurityScoped: Bool
        var lastKnownPath: String
        var name: String
        var typeDescription: String
        var byteCount: Int64?
        var isDirectory: Bool
        var createdAt: Date
        var identity: ShelfFileIdentity?

        init(_ item: ShelfItem) {
            id = item.id; bookmark = item.bookmark; isSecurityScoped = item.isSecurityScoped
            lastKnownPath = item.lastKnownPath; name = item.name; typeDescription = item.typeDescription
            byteCount = item.byteCount; isDirectory = item.isDirectory; createdAt = item.createdAt
            identity = item.identity
        }

        var item: ShelfItem {
            var result = ShelfItem(id: id, bookmark: bookmark, isSecurityScoped: isSecurityScoped,
                      lastKnownPath: lastKnownPath, name: name, typeDescription: typeDescription,
                      byteCount: byteCount, isDirectory: isDirectory, createdAt: createdAt,
                      availabilityError: nil)
            result.identity = identity
            return result
        }
    }

    private static let diagnosticDirectory = FileManager.default.temporaryDirectory
        .appendingPathComponent("EndfieldCharge-FileShelf-\(ProcessInfo.processInfo.processIdentifier)-\(UUID().uuidString)", isDirectory: true)

    static func applicationDirectory() -> URL {
        if CommandLine.arguments.contains(where: {
            $0 == "--ui-test" || $0.hasSuffix("smoke-test") || $0.hasPrefix("--render-")
        }) { return diagnosticDirectory }
        let support = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask).first
            ?? FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent("Library/Application Support", isDirectory: true)
        return support.appendingPathComponent("EndfieldCharge/FileShelf", isDirectory: true)
    }

    init(directory: URL) throws {
        fileURL = directory.appendingPathComponent("shelf.json")
        do {
            try fileManager.createDirectory(at: directory, withIntermediateDirectories: true)
            if fileManager.fileExists(atPath: fileURL.path) {
                let data = try Data(contentsOf: fileURL)
                items = try Self.decode(data)
                persistedData = data
            }
        } catch let error as FileShelfStoreError { throw error }
        catch { throw FileShelfStoreError.persistence(error.localizedDescription) }
    }

    @discardableResult
    func add(urls: [URL]) throws -> Int {
        guard !urls.isEmpty else { return 0 }
        var next = items
        // Resolve existing identities once, rather than for every incoming URL.
        // Unavailable entries remain intact and cannot block adding other items.
        var identities = next.compactMap(\.identity)
        for url in urls {
            guard url.isFileURL else { throw FileShelfStoreError.invalidFile }
            let inputScope = url.startAccessingSecurityScopedResource()
            defer { if inputScope { url.stopAccessingSecurityScopedResource() } }
            let url = url.standardizedFileURL
            var item = try metadata(at: url)
            if let identity = item.identity, identities.contains(where: { $0.matches(identity) }) { continue }
            let bookmark = try createBookmark(at: url)
            item.bookmark = bookmark.data
            item.isSecurityScoped = bookmark.scoped
            next.append(item)
            if let identity = item.identity { identities.append(identity) }
        }
        let added = next.count - items.count
        if added > 0 { try commit(next) }
        return added
    }

    func remove(id: UUID) throws {
        guard items.contains(where: { $0.id == id }) else { return }
        try commit(items.filter { $0.id != id })
    }

    func clear() throws {
        guard !items.isEmpty else { return }
        try commit([])
    }

    /// Availability failures are card state. Persistence failures propagate so
    /// the HUD can report them without claiming that refreshed data was saved.
    func refresh() throws {
        var next = items
        var needsSave = false
        for index in next.indices {
            do {
                let (access, stale) = try resolvedAccess(for: next[index])
                defer { access.close() }
                var refreshed = try metadata(at: access.url, preserving: next[index])
                if stale {
                    // Access remains valid even when renewing a stale bookmark
                    // fails (for example, on a temporarily read-only volume).
                    if let bookmark = try? createBookmark(at: access.url) {
                        refreshed.bookmark = bookmark.data
                        refreshed.isSecurityScoped = bookmark.scoped
                    }
                }
                if Record(refreshed).item != Record(next[index]).item { needsSave = true }
                next[index] = refreshed
            } catch {
                next[index].availabilityError = error.localizedDescription
            }
        }
        if needsSave { try commit(next) }
        else { items = next }
    }

    func access(id: UUID) throws -> ShelfFileAccess {
        guard let item = items.first(where: { $0.id == id }) else { throw FileShelfStoreError.missingItem }
        return try resolvedAccess(for: item).0
    }

    private func resolvedAccess(for item: ShelfItem) throws -> (ShelfFileAccess, Bool) {
        var stale = false
        var options: URL.BookmarkResolutionOptions = [.withoutUI, .withoutMounting]
        if item.isSecurityScoped { options.insert(.withSecurityScope) }
        do {
            let url = try URL(resolvingBookmarkData: item.bookmark, options: options,
                              relativeTo: nil, bookmarkDataIsStale: &stale)
            guard url.isFileURL else { throw FileShelfStoreError.invalidFile }
            let access = ShelfFileAccess(url: url, securityScoped: item.isSecurityScoped)
            do {
                let current = try metadata(at: url)
                if let expected = item.identity, let actual = current.identity, !expected.matches(actual) {
                    throw FileShelfStoreError.replacedItem
                }
                return (access, stale)
            } catch { access.close(); throw error }
        } catch let error as FileShelfStoreError { throw error }
        catch { throw FileShelfStoreError.unavailable(error.localizedDescription) }
    }

    private func metadata(at url: URL, preserving previous: ShelfItem? = nil) throws -> ShelfItem {
        guard url.isFileURL else { throw FileShelfStoreError.invalidFile }
        do {
            guard try url.checkResourceIsReachable(), fileManager.isReadableFile(atPath: url.path) else {
                throw FileShelfStoreError.invalidFile
            }
            let values = try url.resourceValues(forKeys: [.isDirectoryKey, .fileSizeKey,
                .localizedTypeDescriptionKey, .volumeUUIDStringKey])
            var info = stat()
            guard lstat(url.path, &info) == 0 else { throw FileShelfStoreError.invalidFile }
            let kind = info.st_mode & S_IFMT
            guard kind == S_IFREG || kind == S_IFDIR || kind == S_IFLNK else {
                throw FileShelfStoreError.invalidFile
            }
            let isDirectory = values.isDirectory == true
            let identity = ShelfFileIdentity(inode: UInt64(info.st_ino), volumeUUID: values.volumeUUIDString,
                                             device: info.st_dev)
            var result = ShelfItem(id: previous?.id ?? UUID(), bookmark: previous?.bookmark ?? Data(),
                isSecurityScoped: previous?.isSecurityScoped ?? false, lastKnownPath: url.path,
                name: url.lastPathComponent,
                typeDescription: values.localizedTypeDescription ?? (isDirectory ? L10n.text("Folder", "文件夹") : L10n.text("File", "文件")),
                byteCount: isDirectory ? nil : values.fileSize.map(Int64.init), isDirectory: isDirectory,
                createdAt: previous?.createdAt ?? Date(), availabilityError: nil)
            result.identity = identity
            return result
        } catch let error as FileShelfStoreError { throw error }
        catch { throw FileShelfStoreError.unavailable(error.localizedDescription) }
    }

    private func createBookmark(at url: URL) throws -> (data: Data, scoped: Bool) {
        let keys: Set<URLResourceKey> = [.fileResourceIdentifierKey, .volumeUUIDStringKey, .nameKey]
        // Foundation's security-scoped creation dereferences symbolic links.
        // Regular bookmarks preserve the actual Finder link, which is the
        // object an unsandboxed shelf must hand back to a drag destination.
        if !Self.isSandboxed, (try? url.resourceValues(forKeys: [.isSymbolicLinkKey]).isSymbolicLink) == true {
            do {
                return (try url.bookmarkData(options: [], includingResourceValuesForKeys: keys, relativeTo: nil), false)
            } catch { throw FileShelfStoreError.unavailable(error.localizedDescription) }
        }
        do {
            return (try url.bookmarkData(options: [.withSecurityScope, .securityScopeAllowOnlyReadAccess],
                                         includingResourceValuesForKeys: keys, relativeTo: nil), true)
        } catch {
            // A non-sandboxed local build can legitimately use regular persistent
            // bookmarks. Never silently discard sandbox access requirements.
            guard !Self.isSandboxed else { throw FileShelfStoreError.unavailable(error.localizedDescription) }
            do {
                return (try url.bookmarkData(options: [], includingResourceValuesForKeys: keys, relativeTo: nil), false)
            } catch { throw FileShelfStoreError.unavailable(error.localizedDescription) }
        }
    }

    private static var isSandboxed: Bool {
        guard let task = SecTaskCreateFromSelf(nil) else { return true }
        return (SecTaskCopyValueForEntitlement(task, "com.apple.security.app-sandbox" as CFString, nil) as? Bool) == true
    }

    private func commit(_ next: [ShelfItem]) throws {
        do {
            // Refuse to overwrite unexpected or corrupt external changes with an
            // old in-memory snapshot. The previous on-disk bytes remain intact.
            let current = fileManager.fileExists(atPath: fileURL.path) ? try Data(contentsOf: fileURL) : nil
            guard current == persistedData else { throw FileShelfStoreError.changedOnDisk }
            let encoder = JSONEncoder()
            encoder.outputFormatting = [.sortedKeys]
            let data = try encoder.encode(Archive(version: 1, items: next.map(Record.init)))
            try data.write(to: fileURL, options: .atomic)
            persistedData = data
            items = next
        } catch let error as FileShelfStoreError { throw error }
        catch { throw FileShelfStoreError.persistence(error.localizedDescription) }
    }

    private static func decode(_ data: Data) throws -> [ShelfItem] {
        let decoder = JSONDecoder()
        guard let version = try? decoder.decode(Version.self, from: data).version else {
            throw FileShelfStoreError.invalidRecord
        }
        guard version <= 1 else { throw FileShelfStoreError.newerVersion }
        guard version == 1, let archive = try? decoder.decode(Archive.self, from: data),
              Set(archive.items.map(\.id)).count == archive.items.count,
              archive.items.allSatisfy({ !$0.bookmark.isEmpty && $0.lastKnownPath.hasPrefix("/")
                  && !$0.name.isEmpty && $0.createdAt.timeIntervalSinceReferenceDate.isFinite
                  && ($0.byteCount == nil || $0.byteCount! >= 0) }) else {
            throw FileShelfStoreError.invalidRecord
        }
        return archive.items.map(\.item)
    }
}
