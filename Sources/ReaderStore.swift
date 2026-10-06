import Foundation

struct ReaderLocation: Codable, Equatable, Hashable {
    var section = 0, block = 0, character = 0
    var isValid: Bool { section >= 0 && section < 100_000 && block >= 0 && block < 100_000 && character >= 0 && character <= 64_000_000 }
}
struct ReaderPreferences: Codable, Equatable {
    var fontName = "Georgia"
    var fontSize: Double = 10
    var lineSpacing: Double = 2
    var margin: Double = 16
    // Keep the version-1 keys readable. Direction now means the turning axis;
    // the old rightToLeft flag is ignored without rewriting a saved library.
    var rightToLeft = false
    var continuous = true
    var vertical: Bool { get { continuous } set { continuous = newValue; rightToLeft = false } }
    var isValid: Bool { !fontName.isEmpty && fontName.utf8.count <= 256 && fontSize.isFinite && (10...32).contains(fontSize)
        && lineSpacing.isFinite && (0...18).contains(lineSpacing) && margin.isFinite && (6...48).contains(margin) }
}
struct ReaderBookmark: Codable, Equatable, Identifiable {
    var id = UUID()
    let location: ReaderLocation
    let progress: Double
}
struct ReaderBook: Codable, Equatable, Identifiable {
    let id: UUID
    var bookmark: Data
    var scoped: Bool
    var path: String
    var title: String
    var location = ReaderLocation()
    var progress: Double = 0
    var bookmarks: [ReaderBookmark] = []
    var isValid: Bool { !bookmark.isEmpty && bookmark.count <= 256 * 1024 && path.hasPrefix("/") && !path.contains("\0")
        && path.utf8.count <= 32_768 && !title.isEmpty && title.utf8.count <= 4096 && location.isValid
        && progress.isFinite && (0...1).contains(progress) && bookmarks.count <= 128
        && Set(bookmarks.map(\.id)).count == bookmarks.count
        && bookmarks.allSatisfy { $0.location.isValid && $0.progress.isFinite && (0...1).contains($0.progress) } }
}

/// Separate, small reference-only library. Validate before writes, refuse newer
/// schemas and concurrent disk changes, and never modify the referenced books.
final class ReaderStore {
    private struct Archive: Codable { var version = 1; var books: [ReaderBook]; var selected: UUID?; var preferences: ReaderPreferences }
    private(set) var books: [ReaderBook] = []
    private(set) var selected: UUID?
    private(set) var preferences = ReaderPreferences()
    private let url: URL
    private var persisted: Data?
    static let maximumBooks = 50
    private static let diagnosticDirectory = FileManager.default.temporaryDirectory
        .appendingPathComponent("EndfieldHUD-Reader-\(ProcessInfo.processInfo.processIdentifier)-\(UUID().uuidString)")
    static func applicationDirectory(arguments: [String] = CommandLine.arguments) -> URL {
        if arguments.contains(where: { $0 == "--ui-test" || $0.hasSuffix("smoke-test") || $0.hasPrefix("--render-") }) {
            return diagnosticDirectory
        }
        return (FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask).first
            ?? FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent("Library/Application Support"))
            .appendingPathComponent("EndfieldCharge/Reader")
    }
    init(directory: URL) throws {
        url = directory.appendingPathComponent("library.json")
        if FileManager.default.fileExists(atPath: url.path) {
            let data = try read()
            guard let data, let archive = try? JSONDecoder().decode(Archive.self, from: data), archive.version == 1,
                  Self.valid(archive) else { throw ReaderError.invalidBook }
            books = archive.books; selected = archive.selected; preferences = archive.preferences; persisted = data
        }
    }
    @discardableResult func add(url source: URL, title: String) throws -> ReaderBook {
        guard source.isFileURL else { throw ReaderError.unsupported }
        let access = source.startAccessingSecurityScopedResource(); defer { if access { source.stopAccessingSecurityScopedResource() } }
        let data: Data, scoped: Bool
        do { data = try source.bookmarkData(options: [.withSecurityScope], includingResourceValuesForKeys: nil, relativeTo: nil); scoped = true }
        catch { data = try source.bookmarkData(options: [], includingResourceValuesForKeys: nil, relativeTo: nil); scoped = false }
        var next = books
        var book = next.first(where: { $0.path == source.path }) ?? ReaderBook(id: UUID(), bookmark: data, scoped: scoped, path: source.path, title: title)
        book.bookmark = data; book.scoped = scoped; book.title = String(title.prefix(200))
        if let index = next.firstIndex(where: { $0.id == book.id }) { next[index] = book }
        else { guard next.count < Self.maximumBooks else { throw ReaderError.tooLarge }; next.append(book) }
        try commit(books: next, selected: book.id, preferences: preferences); return book
    }
    func select(_ id: UUID) throws { guard books.contains(where: { $0.id == id }) else { throw ReaderError.unavailable }; try commit(books: books, selected: id, preferences: preferences) }
    func remove(_ id: UUID) throws {
        let next = books.filter { $0.id != id }
        if next != books { try commit(books: next, selected: selected == id ? next.first?.id : selected, preferences: preferences) }
    }
    func saveProgress(_ location: ReaderLocation, progress: Double, id: UUID) throws {
        guard let index = books.firstIndex(where: { $0.id == id }), location.isValid, progress.isFinite else { return }
        var next = books; next[index].location = location; next[index].progress = min(1, max(0, progress))
        if next != books { try commit(books: next, selected: selected, preferences: preferences) }
    }
    func toggleBookmark(id: UUID) throws {
        guard let index = books.firstIndex(where: { $0.id == id }) else { return }
        var next = books
        if let found = next[index].bookmarks.firstIndex(where: { $0.location == next[index].location }) { next[index].bookmarks.remove(at: found) }
        else { guard next[index].bookmarks.count < 128 else { throw ReaderError.tooLarge }
            next[index].bookmarks.append(ReaderBookmark(location: next[index].location, progress: next[index].progress)) }
        try commit(books: next, selected: selected, preferences: preferences)
    }
    func setPreferences(_ value: ReaderPreferences) throws {
        guard value.isValid else { throw ReaderError.invalidBook }
        if value != preferences { try commit(books: books, selected: selected, preferences: value) }
    }
    private func commit(books: [ReaderBook], selected: UUID?, preferences: ReaderPreferences) throws {
        let archive = Archive(books: books, selected: selected, preferences: preferences)
        guard Self.valid(archive) else { throw ReaderError.invalidBook }
        guard try read() == persisted else { throw ReaderError.changedOnDisk }
        let data = try JSONEncoder().encode(archive)
        guard data.count <= 4 * 1024 * 1024 else { throw ReaderError.tooLarge }
        try FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
        try data.write(to: url, options: .atomic)
        self.books = books; self.selected = selected; self.preferences = preferences; persisted = data
    }
    private static func valid(_ value: Archive) -> Bool { value.books.count <= maximumBooks && value.books.allSatisfy(\.isValid)
        && Set(value.books.map(\.id)).count == value.books.count && value.preferences.isValid
        && (value.selected == nil || value.books.contains { $0.id == value.selected }) }
    private func read() throws -> Data? {
        guard FileManager.default.fileExists(atPath: url.path) else { return nil }
        let file = try FileHandle(forReadingFrom: url); defer { try? file.close() }
        let data = try file.read(upToCount: 4 * 1024 * 1024 + 1) ?? Data()
        guard data.count <= 4 * 1024 * 1024 else { throw ReaderError.tooLarge }; return data
    }
}

final class ReaderFileAccess {
    let url: URL
    private let scoped: Bool
    init(url: URL) throws {
        guard url.isFileURL else { throw ReaderError.unsupported }
        let acquired = url.startAccessingSecurityScopedResource()
        guard (try? url.resourceValues(forKeys: [.isRegularFileKey]).isRegularFile) == true else {
            if acquired { url.stopAccessingSecurityScopedResource() }; throw ReaderError.unavailable
        }
        self.url = url; scoped = acquired
    }
    convenience init(book: ReaderBook) throws {
        var stale = false
        var options: URL.BookmarkResolutionOptions = [.withoutUI, .withoutMounting]
        if book.scoped { options.insert(.withSecurityScope) }
        let url: URL
        do { url = try URL(resolvingBookmarkData: book.bookmark, options: options, relativeTo: nil, bookmarkDataIsStale: &stale) }
        catch { throw ReaderError.unavailable }
        try self.init(url: url)
    }
    deinit { if scoped { url.stopAccessingSecurityScopedResource() } }
}
