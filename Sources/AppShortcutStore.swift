import AppKit
import Security

enum AppShortcutIcon: String, Codable, CaseIterable {
    case original, bolt, star, terminal, globe, folder, music, play, brush, code, game, camera, grid, textBubble

    var title: String {
        switch self {
        case .original: return L10n.text("App icon", "原始图标")
        case .bolt: return L10n.text("Bolt", "闪电")
        case .star: return L10n.text("Star", "星形")
        case .terminal: return L10n.text("Terminal", "终端")
        case .globe: return L10n.text("Globe", "地球")
        case .folder: return L10n.text("Folder", "文件夹")
        case .music: return L10n.text("Music", "音乐")
        case .play: return L10n.text("Play", "播放")
        case .brush: return L10n.text("Brush", "画笔")
        case .code: return L10n.text("Code", "代码")
        case .game: return L10n.text("Game", "游戏")
        case .camera: return L10n.text("Camera", "相机")
        case .grid: return L10n.text("Grid", "网格")
        case .textBubble: return L10n.text("Text bubble", "文字气泡")
        }
    }
}

struct AppShortcutCandidate {
    let url: URL
    let name: String
    let bundleIdentifier: String?
    let icon: NSImage?
}

struct AppShortcut: Identifiable, Codable, Equatable {
    let id: UUID
    var name: String
    var originalName: String
    var bundleIdentifier: String?
    var iconPreset: AppShortcutIcon
    let createdAt: Date
    fileprivate var bookmark: Data
    fileprivate var securityScoped: Bool
    fileprivate var lastKnownPath: String
}

enum AppShortcutStoreError: LocalizedError {
    case invalidApplication, invalidName, duplicate, missingItem, applicationChanged
    case invalidRecord, newerVersion, changedOnDisk
    case unavailable(String), persistence(String)

    var errorDescription: String? {
        switch self {
        case .invalidApplication:
            return L10n.text("Choose a macOS application (.app) with an available executable.", "请选择包含可用执行文件的 macOS 应用程序（.app）。")
        case .invalidName:
            return L10n.text("Enter a name of 1–128 characters without line breaks.", "请输入 1–128 个字符的名称，不含换行。")
        case .duplicate:
            return L10n.text("This application is already saved. Edit its existing shortcut instead.", "此应用已添加，请编辑现有快捷方式。")
        case .missingItem:
            return L10n.text("This app shortcut is no longer available.", "此应用快捷方式已不存在。")
        case .applicationChanged:
            return L10n.text("A different application now occupies this location. Choose the application again.", "此位置的应用已更改，请重新选择应用。")
        case .invalidRecord:
            return L10n.text("The saved app shortcuts could not be read. The original data has been preserved.", "无法读取应用快捷方式，原始数据已保留。")
        case .newerVersion:
            return L10n.text("These app shortcuts were saved by a newer version of EndfieldCharge.", "这些快捷方式由较新版本的 EndfieldCharge 保存。")
        case .changedOnDisk:
            return L10n.text("App shortcuts changed outside this window. Restart EndfieldCharge before editing.", "应用快捷方式已在外部更改，请重启 EndfieldCharge 后再编辑。")
        case .unavailable(let detail):
            return L10n.text("The application is unavailable: ", "应用不可用：") + detail
        case .persistence(let detail):
            return L10n.text("App shortcuts could not be saved or opened: ", "无法保存或打开应用快捷方式：") + detail
        }
    }
}

/// Bookmarks and small metadata only: no app copying, icon archive, scans or
/// polling. A failed read/write preserves both the file and the previous state.
final class AppShortcutStore {
    private(set) var items: [AppShortcut] = []
    private let fileURL: URL
    private var persistedData: Data?
    private var iconCache: [UUID: NSImage] = [:]
    private let fileManager = FileManager.default
    private struct Archive: Codable { let version: Int; let items: [AppShortcut] }
    private struct Version: Decodable { let version: Int }
    private static let diagnosticDirectory = FileManager.default.temporaryDirectory
        .appendingPathComponent("EndfieldCharge-AppShortcuts-\(ProcessInfo.processInfo.processIdentifier)-\(UUID().uuidString)", isDirectory: true)

    static func applicationDirectory() -> URL {
        if CommandLine.arguments.contains(where: {
            $0 == "--ui-test" || $0.hasSuffix("smoke-test") || $0.hasPrefix("--render-")
        }) { return diagnosticDirectory }
        let support = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask).first
            ?? FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent("Library/Application Support", isDirectory: true)
        return support.appendingPathComponent("EndfieldCharge/AppShortcuts", isDirectory: true)
    }

    init(directory: URL) throws {
        fileURL = directory.appendingPathComponent("shortcuts.json")
        do {
            try fileManager.createDirectory(at: directory, withIntermediateDirectories: true)
            if fileManager.fileExists(atPath: fileURL.path) {
                let data = try Data(contentsOf: fileURL)
                items = try Self.decode(data)
                persistedData = data
            }
        } catch let error as AppShortcutStoreError { throw error }
        catch { throw AppShortcutStoreError.persistence(error.localizedDescription) }
    }

    func inspect(url: URL) throws -> AppShortcutCandidate {
        guard url.isFileURL else { throw AppShortcutStoreError.invalidApplication }
        let scoped = url.startAccessingSecurityScopedResource()
        defer { if scoped { url.stopAccessingSecurityScopedResource() } }
        let url = url.standardizedFileURL.resolvingSymlinksInPath()
        let metadata = try Self.applicationMetadata(at: url)
        return AppShortcutCandidate(url: url, name: metadata.name,
                                    bundleIdentifier: metadata.bundleIdentifier,
                                    icon: NSWorkspace.shared.icon(forFile: url.path))
    }

    @discardableResult
    func save(candidate: AppShortcutCandidate, name: String, iconPreset: AppShortcutIcon,
              editingID: UUID? = nil) throws -> AppShortcut {
        let name = name.trimmingCharacters(in: .whitespacesAndNewlines)
        guard Self.validName(name) else { throw AppShortcutStoreError.invalidName }
        if let editingID, !items.contains(where: { $0.id == editingID }) { throw AppShortcutStoreError.missingItem }
        let scoped = candidate.url.startAccessingSecurityScopedResource()
        defer { if scoped { candidate.url.stopAccessingSecurityScopedResource() } }
        // Do not trust a stale draft if an application was removed or replaced.
        let verified = try inspect(url: candidate.url)
        guard verified.bundleIdentifier == candidate.bundleIdentifier else { throw AppShortcutStoreError.applicationChanged }
        for item in items where item.id != editingID {
            if Self.canonical(URL(fileURLWithPath: item.lastKnownPath)) == verified.url
                || (try? resolve(item).url) == verified.url { throw AppShortcutStoreError.duplicate }
        }
        let bookmark = try createBookmark(at: verified.url)
        let previous = items.first { $0.id == editingID }
        let result = AppShortcut(id: previous?.id ?? UUID(), name: name, originalName: verified.name,
                                 bundleIdentifier: verified.bundleIdentifier, iconPreset: iconPreset,
                                 createdAt: previous?.createdAt ?? Date(), bookmark: bookmark.data,
                                 securityScoped: bookmark.scoped, lastKnownPath: verified.url.path)
        var next = items
        if let index = next.firstIndex(where: { $0.id == result.id }) { next[index] = result }
        else { next.append(result) }
        try commit(next)
        iconCache[result.id] = verified.icon
        return result
    }

    func remove(id: UUID) throws {
        guard items.contains(where: { $0.id == id }) else { return }
        try commit(items.filter { $0.id != id })
        iconCache.removeValue(forKey: id)
    }

    /// Resolves bookmarks without UI or mounting unavailable volumes. Missing
    /// applications remain editable/removable; they never launch a substitute.
    func resolvedURL(for id: UUID) throws -> URL {
        guard let index = items.firstIndex(where: { $0.id == id }) else { throw AppShortcutStoreError.missingItem }
        let item = items[index]
        let result = try resolve(item)
        if result.stale || result.url.path != item.lastKnownPath {
            let scoped = result.url.startAccessingSecurityScopedResource()
            defer { if scoped { result.url.stopAccessingSecurityScopedResource() } }
            let bookmark = try createBookmark(at: result.url)
            var next = items
            next[index].bookmark = bookmark.data
            next[index].securityScoped = bookmark.scoped
            next[index].lastKnownPath = result.url.path
            try commit(next)
            iconCache.removeValue(forKey: id)
        }
        return result.url
    }

    func icon(for id: UUID) -> NSImage? {
        if let cached = iconCache[id] { return cached }
        guard let item = items.first(where: { $0.id == id }), let result = try? resolve(item) else { return nil }
        let scoped = result.url.startAccessingSecurityScopedResource()
        defer { if scoped { result.url.stopAccessingSecurityScopedResource() } }
        let image = NSWorkspace.shared.icon(forFile: result.url.path)
        iconCache[id] = image
        return image
    }

    private func resolve(_ item: AppShortcut) throws -> (url: URL, stale: Bool) {
        do {
            var stale = false
            var options: URL.BookmarkResolutionOptions = [.withoutUI, .withoutMounting]
            if item.securityScoped { options.insert(.withSecurityScope) }
            let bookmarked = try URL(resolvingBookmarkData: item.bookmark, options: options,
                                     relativeTo: nil, bookmarkDataIsStale: &stale)
            let scoped = bookmarked.startAccessingSecurityScopedResource()
            defer { if scoped { bookmarked.stopAccessingSecurityScopedResource() } }
            let url = Self.canonical(bookmarked)
            let metadata = try Self.applicationMetadata(at: url)
            guard metadata.bundleIdentifier == item.bundleIdentifier else { throw AppShortcutStoreError.applicationChanged }
            return (url, stale)
        } catch let error as AppShortcutStoreError { throw error }
        catch { throw AppShortcutStoreError.unavailable(error.localizedDescription) }
    }

    /// Shared with the launcher so a saved path cannot silently become a file,
    /// URL handler or non-application package between selection and launch.
    static func applicationMetadata(at url: URL) throws -> (name: String, bundleIdentifier: String?) {
        guard url.isFileURL, url.pathExtension.lowercased() == "app" else { throw AppShortcutStoreError.invalidApplication }
        do {
            let values = try url.resourceValues(forKeys: [.isDirectoryKey, .isReadableKey])
            guard values.isDirectory == true, values.isReadable == true else { throw AppShortcutStoreError.invalidApplication }
            let infoURL = url.appendingPathComponent("Contents/Info.plist")
            guard let info = try PropertyListSerialization.propertyList(from: Data(contentsOf: infoURL), options: [], format: nil) as? [String: Any],
                  info["CFBundlePackageType"] as? String == "APPL",
                  let executable = info["CFBundleExecutable"] as? String, !executable.isEmpty,
                  executable != ".", executable != "..", !executable.contains("/"), !executable.contains("\0") else {
                throw AppShortcutStoreError.invalidApplication
            }
            let executableURL = url.appendingPathComponent("Contents/MacOS").appendingPathComponent(executable)
            let executableValues = try executableURL.resourceValues(forKeys: [.isRegularFileKey])
            guard executableValues.isRegularFile == true,
                  FileManager.default.isExecutableFile(atPath: executableURL.path) else { throw AppShortcutStoreError.invalidApplication }
            let localized = Bundle(url: url)?.localizedInfoDictionary
            let names = [localized?["CFBundleDisplayName"], localized?["CFBundleName"],
                         info["CFBundleDisplayName"], info["CFBundleName"]]
            let name = names.compactMap { $0 as? String }.map { $0.trimmingCharacters(in: .whitespacesAndNewlines) }
                .first { !$0.isEmpty } ?? url.deletingPathExtension().lastPathComponent
            let bundleID = (info["CFBundleIdentifier"] as? String).flatMap { $0.isEmpty ? nil : $0 }
            return (name, bundleID)
        } catch let error as AppShortcutStoreError { throw error }
        catch { throw AppShortcutStoreError.unavailable(error.localizedDescription) }
    }

    private func createBookmark(at url: URL) throws -> (data: Data, scoped: Bool) {
        let keys: Set<URLResourceKey> = [.fileResourceIdentifierKey, .volumeUUIDStringKey, .nameKey]
        do {
            return (try url.bookmarkData(options: [.withSecurityScope, .securityScopeAllowOnlyReadAccess],
                                         includingResourceValuesForKeys: keys, relativeTo: nil), true)
        } catch {
            guard !Self.isSandboxed else { throw AppShortcutStoreError.unavailable(error.localizedDescription) }
            do { return (try url.bookmarkData(options: [], includingResourceValuesForKeys: keys, relativeTo: nil), false) }
            catch { throw AppShortcutStoreError.unavailable(error.localizedDescription) }
        }
    }

    private static var isSandboxed: Bool {
        guard let task = SecTaskCreateFromSelf(nil) else { return true }
        return (SecTaskCopyValueForEntitlement(task, "com.apple.security.app-sandbox" as CFString, nil) as? Bool) == true
    }

    private static func canonical(_ url: URL) -> URL { url.standardizedFileURL.resolvingSymlinksInPath() }
    private static func validName(_ name: String) -> Bool {
        !name.isEmpty && name.count <= 128 && name.rangeOfCharacter(from: .controlCharacters) == nil
    }

    private func commit(_ next: [AppShortcut]) throws {
        do {
            let current = fileManager.fileExists(atPath: fileURL.path) ? try Data(contentsOf: fileURL) : nil
            guard current == persistedData else { throw AppShortcutStoreError.changedOnDisk }
            let encoder = JSONEncoder()
            encoder.outputFormatting = [.sortedKeys]
            let data = try encoder.encode(Archive(version: 1, items: next))
            try data.write(to: fileURL, options: .atomic)
            persistedData = data
            items = next
        } catch let error as AppShortcutStoreError { throw error }
        catch { throw AppShortcutStoreError.persistence(error.localizedDescription) }
    }

    private static func decode(_ data: Data) throws -> [AppShortcut] {
        let decoder = JSONDecoder()
        guard let version = try? decoder.decode(Version.self, from: data).version else { throw AppShortcutStoreError.invalidRecord }
        guard version <= 1 else { throw AppShortcutStoreError.newerVersion }
        guard version == 1, let archive = try? decoder.decode(Archive.self, from: data),
              Set(archive.items.map(\.id)).count == archive.items.count,
              archive.items.allSatisfy({ validName($0.name) && !$0.originalName.isEmpty
                  && !$0.bookmark.isEmpty && $0.lastKnownPath.hasPrefix("/")
                  && URL(fileURLWithPath: $0.lastKnownPath).pathExtension.lowercased() == "app"
                  && $0.createdAt.timeIntervalSinceReferenceDate.isFinite }) else { throw AppShortcutStoreError.invalidRecord }
        return archive.items
    }
}
