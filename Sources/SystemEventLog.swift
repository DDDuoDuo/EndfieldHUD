import Foundation

enum SystemEventCategory: String, Codable, CaseIterable {
    case navigation, clipboard, files, work, power, audio, display
    var title: String {
        switch self {
        case .navigation: return "Navigation"
        case .clipboard: return "Clipboard"
        case .files: return "Files"
        case .work: return "Work"
        case .power: return "Power"
        case .audio: return "Audio"
        case .display: return "Display"
        }
    }
}

enum SystemEventKind: String, Codable, CaseIterable {
    case overlayOpened, moduleOpened, appShortcutOpened, clipboardCopied
    case shelfAdded, shelfRemoved, shelfCleared
    case workStarted, workPaused, workResumed, workReset, workCompleted
    case powerConnected, powerDisconnected, batteryStateChanged
    case audioDeviceConnected, audioDeviceDisconnected, displayConnected, displayDisconnected

    var category: SystemEventCategory {
        switch self {
        case .overlayOpened, .moduleOpened, .appShortcutOpened: return .navigation
        case .clipboardCopied: return .clipboard
        case .shelfAdded, .shelfRemoved, .shelfCleared: return .files
        case .workStarted, .workPaused, .workResumed, .workReset, .workCompleted: return .work
        case .powerConnected, .powerDisconnected, .batteryStateChanged: return .power
        case .audioDeviceConnected, .audioDeviceDisconnected: return .audio
        case .displayConnected, .displayDisconnected: return .display
        }
    }

    var title: String {
        switch self {
        case .overlayOpened: return "Overlay opened"
        case .moduleOpened: return "Module opened"
        case .appShortcutOpened: return "App shortcut opened"
        case .clipboardCopied: return "Clipboard item copied"
        case .shelfAdded: return "Added to file shelf"
        case .shelfRemoved: return "Removed from file shelf"
        case .shelfCleared: return "File shelf cleared"
        case .workStarted: return "Work timer started"
        case .workPaused: return "Work timer paused"
        case .workResumed: return "Work timer resumed"
        case .workReset: return "Work timer reset"
        case .workCompleted: return "Work timer completed"
        case .powerConnected: return "Power connected"
        case .powerDisconnected: return "Power disconnected"
        case .batteryStateChanged: return "Battery state changed"
        case .audioDeviceConnected: return "Audio device connected"
        case .audioDeviceDisconnected: return "Audio device disconnected"
        case .displayConnected: return "Display connected"
        case .displayDisconnected: return "Display disconnected"
        }
    }
}

struct SystemEvent: Identifiable, Codable, Equatable {
    let id: UUID
    let kind: SystemEventKind
    let createdAt: Date
    let metadata: [String: String]
    var category: SystemEventCategory { kind.category }
    var title: String { kind.title }
    var detail: String {
        var fields: [String] = []
        if let module = metadata["module"].flatMap(HUDModule.init(rawValue:)) { fields.append(module.englishTitle) }
        if let name = metadata["app"] { fields.append(name) }
        if let name = metadata["filename"] { fields.append(name) }
        if let name = metadata["device"] { fields.append(name) }
        if let type = metadata["kind"] {
            let titles = ["text": "Text", "url": "Link",
                          "image": "Image", "files": "Files",
                          "countdown": "Countdown", "stopwatch": "Stopwatch"]
            if let title = titles[type] { fields.append(title) }
        }
        if let seconds = metadata["seconds"].flatMap(Int.init) {
            fields.append(String(format: "%02d:%02d:%02d", seconds / 3600, (seconds / 60) % 60, seconds % 60))
        }
        if let state = metadata["state"] {
            let titles = ["charging": "Charging", "full": "Full",
                          "connected": "Connected", "battery": "On battery",
                          "noBattery": "No battery", "unavailable": "Unavailable"]
            if let title = titles[state] { fields.append(title) }
        }
        if let percentage = metadata["percentage"] { fields.append(percentage + "%") }
        if let count = metadata["count"] { fields.append("\(count) items") }
        return fields.joined(separator: " · ")
    }
}

/// Main-thread model. Only short, allowlisted metadata reaches the bounded local
/// file. A serial writer owns disk I/O; pending snapshots never retain the UI.
final class SystemEventLog {
    private struct Document: Codable { let version: Int; let events: [SystemEvent] }
    private(set) var events: [SystemEvent] = []
    let capacity: Int
    private var observers: [UUID: () -> Void] = [:]
    private var failure: Failure?
    private var revision: UInt64 = 0
    private var pending: DispatchWorkItem?
    private let writer: Writer?
    private enum Failure { case load, version, save }
    var statusMessage: String? {
        switch failure {
        case .load?: return "Saved log could not be read; the original file is preserved."
        case .version?: return "This log uses a newer format; the original file is preserved."
        case .save?: return "The log could not be saved. New events are kept in this session."
        case nil: return nil
        }
    }

    init(directory: URL? = nil, capacity: Int = 500) {
        precondition(Thread.isMainThread)
        self.capacity = min(500, max(1, capacity))
        guard let directory else { writer = nil; return }
        let url = directory.appendingPathComponent("events.json", isDirectory: false)
        var blocked = false
        if FileManager.default.fileExists(atPath: url.path) {
            do {
                let size = try url.resourceValues(forKeys: [.fileSizeKey]).fileSize ?? Int.max
                guard size <= 2 * 1024 * 1024 else { throw CocoaError(.fileReadTooLarge) }
                let data = try Data(contentsOf: url)
                // Inspect version before decoding enum cases introduced by a newer app.
                let object = try JSONSerialization.jsonObject(with: data) as? [String: Any]
                guard let version = object?["version"] as? Int else { throw CocoaError(.fileReadCorruptFile) }
                if version > 1 { failure = .version; blocked = true }
                else {
                    guard version == 1 else { throw CocoaError(.fileReadCorruptFile) }
                    let document = try JSONDecoder().decode(Document.self, from: data)
                    var seen = Set<UUID>()
                    events = document.events.filter { $0.createdAt.timeIntervalSince1970.isFinite && seen.insert($0.id).inserted }
                        .sorted { $0.createdAt > $1.createdAt }.prefix(self.capacity).map {
                            SystemEvent(id: $0.id, kind: $0.kind, createdAt: $0.createdAt,
                                        metadata: Self.sanitized($0.metadata, kind: $0.kind))
                        }
                }
            } catch { failure = .load; blocked = true }
        }
        writer = Writer(url: url, blocked: blocked)
    }

    static func applicationDirectory() -> URL {
        FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask).first!
            .appendingPathComponent("EndfieldCharge/EventLog", isDirectory: true)
    }

    func record(kind: SystemEventKind, metadata: [String: String] = [:]) {
        precondition(Thread.isMainThread)
        events.insert(SystemEvent(id: UUID(), kind: kind, createdAt: Date(), metadata: Self.sanitized(metadata, kind: kind)), at: 0)
        if events.count > capacity { events.removeLast(events.count - capacity) }
        changed()
    }

    func clear() {
        precondition(Thread.isMainThread)
        events.removeAll()
        changed() // Even an empty log persists an explicit clear; never records itself.
    }

    @discardableResult func observe(_ callback: @escaping () -> Void) -> UUID {
        precondition(Thread.isMainThread)
        let id = UUID(); observers[id] = callback; return id
    }
    func removeObserver(_ id: UUID) { precondition(Thread.isMainThread); observers.removeValue(forKey: id) }

    func flush(completion: ((Bool) -> Void)? = nil) {
        precondition(Thread.isMainThread)
        pending?.cancel(); pending = nil
        guard let writer else { completion?(true); return }
        let document = Document(version: 1, events: events), token = revision
        writer.queue.async { [weak self] in
            let success = writer.write(document, revision: token)
            DispatchQueue.main.async { [weak self] in
                self?.saved(success, revision: token)
                completion?(success)
            }
        }
    }

    /// Termination only. Interactive callers use flush(completion:).
    @discardableResult func flushSynchronously() -> Bool {
        precondition(Thread.isMainThread)
        pending?.cancel(); pending = nil
        guard let writer else { return true }
        let document = Document(version: 1, events: events), token = revision
        let success = writer.queue.sync { writer.write(document, revision: token) }
        saved(success, revision: token)
        return success
    }

    private func changed() {
        revision &+= 1
        pending?.cancel()
        if let writer {
            let document = Document(version: 1, events: events), token = revision
            let work = DispatchWorkItem { [weak self] in
                let success = writer.write(document, revision: token)
                DispatchQueue.main.async { [weak self] in self?.saved(success, revision: token) }
            }
            pending = work
            writer.queue.asyncAfter(deadline: .now() + 0.35, execute: work)
        }
        notify()
    }
    private func saved(_ success: Bool, revision token: UInt64) {
        guard token == revision, failure != .load, failure != .version else { return }
        let prior = failure
        failure = success ? nil : .save
        if prior != failure { notify() }
    }
    private func notify() { for callback in Array(observers.values) { callback() } }

    private static func sanitized(_ raw: [String: String], kind: SystemEventKind) -> [String: String] {
        var result: [String: String] = [:]
        func oneOf(_ key: String, _ allowed: Set<String>) {
            if let value = raw[key], allowed.contains(value) { result[key] = value }
        }
        func number(_ key: String, _ range: ClosedRange<Int>) {
            if let value = raw[key], value.utf8.count <= 12, let number = Int(value), range.contains(number) { result[key] = String(number) }
        }
        switch kind {
        case .moduleOpened: oneOf("module", Set(HUDModule.allCases.map(\.rawValue)))
        case .appShortcutOpened:
            if let value = raw["app"], value.utf8.count <= 4096, !value.contains("://"), !value.contains("/"), !value.contains("\\") {
                let name = compact(value)
                if !name.isEmpty { result["app"] = name }
            }
        case .clipboardCopied: oneOf("kind", ["text", "url", "image", "files"])
        case .shelfAdded, .shelfRemoved:
            if let value = raw["filename"], value.utf8.count <= 4096, !value.contains("://") {
                let basename = value.replacingOccurrences(of: "\\", with: "/").split(separator: "/").last.map(String.init) ?? ""
                let name = compact(basename)
                if !name.isEmpty { result["filename"] = name }
            }
        case .shelfCleared: number("count", 0...1_000_000)
        case .workStarted, .workPaused, .workResumed, .workReset, .workCompleted:
            oneOf("kind", ["countdown", "stopwatch"]); number("seconds", 0...31_536_000)
        case .powerConnected, .powerDisconnected, .batteryStateChanged:
            oneOf("state", ["charging", "full", "connected", "battery", "noBattery", "unavailable"])
            number("percentage", 0...100)
        case .audioDeviceConnected, .audioDeviceDisconnected, .displayConnected, .displayDisconnected:
            if let value = raw["device"], value.utf8.count <= 4096, !value.contains("://"), !value.contains("/"), !value.contains("\\") {
                let name = compact(value)
                if !name.isEmpty { result["device"] = name }
            }
        case .overlayOpened: break
        }
        return result
    }

    private static func compact(_ value: String) -> String {
        var output = "", bytes = 0, lastWasSpace = true
        // Bound inspection as well as output when a caller provides a huge field.
        for character in value.prefix(512) {
            let isSpace = character.unicodeScalars.allSatisfy { CharacterSet.whitespacesAndNewlines.union(.controlCharacters).contains($0) }
            let part = isSpace ? " " : String(character.unicodeScalars.filter { !CharacterSet.controlCharacters.contains($0) })
            if part.isEmpty || (isSpace && lastWasSpace) { continue }
            let size = part.utf8.count
            guard bytes + size <= 160 else { break }
            output += part; bytes += size; lastWasSpace = isSpace
        }
        return output.trimmingCharacters(in: .whitespaces)
    }

    private final class Writer {
        let queue = DispatchQueue(label: "EndfieldCharge.EventLog.persistence", qos: .utility)
        let url: URL
        let blocked: Bool
        private var savedRevision: UInt64?
        init(url: URL, blocked: Bool) { self.url = url; self.blocked = blocked }
        func write(_ document: Document, revision: UInt64) -> Bool {
            guard !blocked else { return false }
            if let savedRevision, revision <= savedRevision { return true }
            do {
                try FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
                let encoder = JSONEncoder(); encoder.outputFormatting = [.sortedKeys]
                let data = try encoder.encode(document)
                try data.write(to: url, options: .atomic)
                savedRevision = revision
                return true
            } catch { return false }
        }
    }
}
