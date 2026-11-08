import AppKit
import Darwin

/// Validates and folds the adapter's bounded JSON-lines protocol. Diffs preserve
/// unchanged album art; a new full payload always starts a new identity.
struct NowPlayingAdapterState {
    /// A line with the largest accepted cover (base64, plus room for JSON
    /// escapes) and the rest of its payload.
    static let maximumLineBytes = ((NowPlayingArtworkLoader.maximumEmbeddedBytes + 2) / 3) * 4 + 2 * 1024 * 1024
    private(set) var payload: [String: Any] = [:]
    private(set) var artwork: Data?
    private(set) var revision = 0
    private(set) var received = false
    mutating func accept(_ data: Data, date: Date = Date()) -> Bool {
        guard data.count <= Self.maximumLineBytes,
              let message = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
              message["type"] as? String == "data", let incoming = message["payload"] as? [String: Any], incoming.count <= 128 else { return false }
        let diff = message["diff"] as? Bool == true
        var next = diff ? payload : [:]
        for (key, value) in incoming {
            guard key.utf8.count <= 256 else { return false }
            if value is NSNull { next.removeValue(forKey: key) } else { next[key] = value }
        }
        if diff, incoming["playing"] != nil, incoming["elapsedTimeMicros"] == nil,
           incoming["timestampEpochMicros"] == nil, let old = track(date: date) {
            next["elapsedTimeMicros"] = (old.elapsed(at: ProcessInfo.processInfo.systemUptime) ?? old.position ?? 0) * 1_000_000
            next["timestampEpochMicros"] = date.timeIntervalSince1970 * 1_000_000
        }
        for key in ["title", "artist", "album", "bundleIdentifier", "parentApplicationBundleIdentifier", "uniqueIdentifier", "contentItemIdentifier"] {
            if let value = next[key] as? String, value.utf8.count > 4096 { return false }
        }
        let oldArtwork = artwork
        if !diff || incoming["artworkData"] != nil {
            if let encoded = next["artworkData"] as? String,
               encoded.utf8.count <= ((NowPlayingArtworkLoader.maximumEmbeddedBytes + 2) / 3) * 4 {
                artwork = Data(base64Encoded: encoded).flatMap { $0.count <= NowPlayingArtworkLoader.maximumEmbeddedBytes ? $0 : nil }
            } else { artwork = nil }
        }
        next.removeValue(forKey: "artworkData") // Keep one decoded compressed payload, not duplicate base64.
        if oldArtwork != artwork { revision &+= 1 }
        let changed = !received || !NSDictionary(dictionary: payload).isEqual(to: next) || oldArtwork != artwork
        payload = next; received = true
        return changed
    }
    mutating func clear() -> Bool {
        let changed = received || !payload.isEmpty || artwork != nil
        payload.removeAll(); artwork = nil; received = false; revision &+= 1
        return changed
    }
    func track(now: TimeInterval = ProcessInfo.processInfo.systemUptime, date: Date = Date()) -> NowPlayingTrack? {
        guard let title = payload["title"] as? String, !title.isEmpty else { return nil }
        let playing = payload["playing"] as? Bool ?? false
        let duration = (payload["durationMicros"] as? NSNumber)?.doubleValue.mapSeconds
            ?? (payload["duration"] as? NSNumber)?.doubleValue
        var elapsed = (payload["elapsedTimeMicros"] as? NSNumber)?.doubleValue.mapSeconds
            ?? (payload["elapsedTime"] as? NSNumber)?.doubleValue
        if playing, let timestamp = (payload["timestampEpochMicros"] as? NSNumber)?.doubleValue,
           let position = elapsed {
            let delta = date.timeIntervalSince1970 - timestamp / 1_000_000
            if delta.isFinite, delta >= 0, delta <= 604_800 { elapsed = position + delta }
        }
        return NowPlayingTrack(title: title, artist: payload["artist"] as? String ?? "", album: payload["album"] as? String ?? "",
            duration: duration, position: elapsed, isPlaying: playing, sampledAt: now,
            identifier: payload["uniqueIdentifier"] as? String,
            timedLyrics: payload["lyrics"] as? String, artworkRevision: artwork.map { _ in String(revision) })
    }
}
private extension Double { var mapSeconds: Double { self / 1_000_000 } }

/// Splits the adapter's output into lines of at most `maximumLineBytes`. A line
/// that passes the limit inside its cover loses only the cover: the
/// `artworkData` value becomes `null` as it streams in, so the track still
/// updates without art. Any other overlong line fails the stream.
struct NowPlayingAdapterLineBuffer {
    private static let artworkKey = Data("\"artworkData\":\"".utf8)
    private static let droppedArtwork = Data("\"artworkData\":null".utf8)
    private(set) var pending = Data()
    private(set) var droppingArtwork = false
    /// The lines `data` completes, or nil when a line passes the limit outside a cover.
    mutating func append(_ data: Data) -> [Data]? {
        var chunk = data[...]
        if droppingArtwork { // Still inside a dropped cover: skip to its closing quote.
            guard let quote = chunk.firstIndex(of: 0x22) else { return [] }
            chunk = chunk[chunk.index(after: quote)...]; droppingArtwork = false
        }
        pending.append(contentsOf: chunk)
        var lines: [Data] = []
        if chunk.contains(10) {
            while let newline = pending.firstIndex(of: 10) {
                var line = Data(pending[..<newline]); pending.removeSubrange(...newline)
                if line.count > NowPlayingAdapterState.maximumLineBytes {
                    guard Self.dropArtwork(in: &line) == .closed,
                          line.count <= NowPlayingAdapterState.maximumLineBytes else { removeAll(); return nil }
                }
                lines.append(line)
            }
        }
        guard pending.count > NowPlayingAdapterState.maximumLineBytes else { return lines }
        switch Self.dropArtwork(in: &pending) {
        case .missing: removeAll(); return nil
        case .open: droppingArtwork = true
        case .closed: break
        }
        guard pending.count <= NowPlayingAdapterState.maximumLineBytes else { removeAll(); return nil }
        return lines
    }
    private enum Drop { case missing, closed, open }
    /// Replaces the cover in `line` with `null`; `.open` when its value runs
    /// past the end of `line`.
    private static func dropArtwork(in line: inout Data) -> Drop {
        guard let key = line.range(of: artworkKey) else { return .missing }
        if let quote = line[key.upperBound...].firstIndex(of: 0x22) {
            line.replaceSubrange(key.lowerBound...quote, with: droppedArtwork); return .closed
        }
        line.replaceSubrange(key.lowerBound..., with: droppedArtwork); return .open
    }
    mutating func removeAll() { pending.removeAll(); droppingArtwork = false }
}

/// One native notification stream only while the module is visible. The helper
/// is bundled source-built MediaRemoteAdapter (BSD-3-Clause), hosted by the OS
/// Perl interpreter for macOS compatibility. No repeated get/spawn polling.
final class NowPlayingAdapter {
    private let queue = DispatchQueue(label: "EndfieldHUD.NowPlayingAdapter", qos: .utility)
    private let lock = NSLock()
    private var state = NowPlayingAdapterState()
    private var process: Process?
    private var readHandle: FileHandle?
    private var input = NowPlayingAdapterLineBuffer()
    private var generation = 0
    private var callback: (() -> Void)?
    private var streaming = false
    private let script: URL?
    private let framework: URL?
    var isAvailable: Bool { script != nil && framework != nil && FileManager.default.isExecutableFile(atPath: "/usr/bin/perl") }
    var awaitsSnapshot: Bool { lock.lock(); defer { lock.unlock() }; return streaming && !state.received }
    var hasSnapshot: Bool { lock.lock(); defer { lock.unlock() }; return state.received }

    init(bundle: Bundle = .main) {
        let candidateScript = bundle.resourceURL?.appendingPathComponent("NowPlaying/mediaremote-adapter.pl")
        let candidateFramework = bundle.privateFrameworksURL?.appendingPathComponent("MediaRemoteAdapter.framework")
        script = candidateScript.flatMap { FileManager.default.fileExists(atPath: $0.path) ? $0 : nil }
        framework = candidateFramework.flatMap { FileManager.default.fileExists(atPath: $0.appendingPathComponent("MediaRemoteAdapter").path) ? $0 : nil }
    }
    func start(_ callback: @escaping () -> Void) -> (() -> Void) {
        precondition(Thread.isMainThread)
        guard isAvailable, process == nil, let script, let framework else { return {} }
        lock.lock(); generation &+= 1; let expected = generation; streaming = true; lock.unlock()
        self.callback = callback
        let child = Process(), pipe = Pipe()
        child.executableURL = URL(fileURLWithPath: "/usr/bin/perl")
        child.arguments = [script.path, framework.path, "stream", "--debounce=120", "--micros"]
        readHandle = pipe.fileHandleForReading
        child.standardOutput = pipe; child.standardError = FileHandle.nullDevice; child.standardInput = FileHandle.nullDevice
        pipe.fileHandleForReading.readabilityHandler = { [weak self] handle in
            let data = handle.availableData
            guard !data.isEmpty else { handle.readabilityHandler = nil; return }
            self?.queue.async { [weak self] in self?.receive(data, expected: expected) }
        }
        child.terminationHandler = { [weak self] _ in
            pipe.fileHandleForReading.readabilityHandler = nil
            DispatchQueue.main.async { [weak self] in
                guard let self, self.matches(expected) else { return }
                self.process = nil; self.readHandle = nil
                self.lock.lock(); self.streaming = false; self.input.removeAll(); _ = self.state.clear(); self.lock.unlock()
                self.callback?()
            }
        }
        do { try child.run(); process = child }
        catch { pipe.fileHandleForReading.readabilityHandler = nil; readHandle = nil; self.callback = nil; lock.lock(); streaming = false; lock.unlock() }
        return { [weak self] in self?.stop() }
    }
    private func receive(_ data: Data, expected: Int) {
        lock.lock()
        guard generation == expected else { lock.unlock(); return }
        guard let lines = input.append(data) else {
            lock.unlock()
            DispatchQueue.main.async { [weak self] in
                guard let self, self.matches(expected) else { return }; self.stop()
            }; return
        }
        var changed = false
        for line in lines { changed = state.accept(line) || changed }
        lock.unlock()
        if changed { DispatchQueue.main.async { [weak self] in
            guard let self, self.matches(expected) else { return }; self.callback?()
        } }
    }
    func snapshot() -> (NowPlayingApplication, NowPlayingTrack)? {
        lock.lock(); let value = state; lock.unlock()
        guard let track = value.track(), let id = value.payload["bundleIdentifier"] as? String,
              let app = NSRunningApplication.runningApplications(withBundleIdentifier: id).first,
              !app.isTerminated, let url = app.bundleURL else { return nil }
        return (NowPlayingApplication(source: NowPlayingSource.allCases.first { $0.bundleIdentifier == id } ?? .system,
            pid: app.processIdentifier, bundleURL: url, bundleIdentifier: id, displayName: app.localizedName), track)
    }
    func artwork(for track: NowPlayingTrack, in app: NowPlayingApplication) -> NowPlayingArtworkPayload? {
        lock.lock(); let value = state; lock.unlock()
        guard value.payload["bundleIdentifier"] as? String == app.bundleIdentifier,
              value.track()?.hasSameIdentity(as: track) == true, let bytes = value.artwork else { return nil }
        return .embedded(bytes)
    }
    func perform(_ command: NowPlayingCommand, in app: NowPlayingApplication, cancelled: () -> Bool) throws {
        guard !cancelled(), let current = snapshot(), current.0 == app, let script, let framework else { throw NowPlayingFailure.unavailable }
        let arguments: [String]
        switch command {
        case .playPause: arguments = ["send", "2"]
        case .next: arguments = ["send", "4"]
        case .previous: arguments = ["send", "5"]
        case .seek(let time):
            guard time.isFinite, time >= 0, time <= 604_800 else { throw NowPlayingFailure.unsupported }
            arguments = ["seek", String(Int64((time * 1_000_000).rounded()))]
        }
        let child = Process(), finished = DispatchSemaphore(value: 0)
        child.executableURL = URL(fileURLWithPath: "/usr/bin/perl")
        child.arguments = [script.path, framework.path] + arguments
        child.standardOutput = FileHandle.nullDevice; child.standardError = FileHandle.nullDevice; child.standardInput = FileHandle.nullDevice
        child.terminationHandler = { _ in finished.signal() }
        do { try child.run() } catch { throw NowPlayingFailure.unavailable }
        let deadline = ProcessInfo.processInfo.systemUptime + 3
        while finished.wait(timeout: .now() + 0.05) != .success {
            if cancelled() { Self.shutdown(child); throw NowPlayingFailure.cancelled }
            if ProcessInfo.processInfo.systemUptime >= deadline { Self.shutdown(child); throw NowPlayingFailure.timedOut }
        }
        guard child.terminationStatus == 0, !cancelled() else { throw NowPlayingFailure.unavailable }
    }
    /// Clears the state the way a helper exit does: the artwork revision keeps
    /// counting, so the next stream never reuses a revision this adapter has
    /// already reported, nor one the artwork loader cached as missing.
    private func stop() {
        lock.lock(); generation &+= 1; streaming = false; input.removeAll(); _ = state.clear(); lock.unlock()
        callback = nil
        let child = process; process = nil
        readHandle?.readabilityHandler = nil; readHandle = nil
        if let child { Self.shutdown(child) }
    }
    private func matches(_ expected: Int) -> Bool { lock.lock(); defer { lock.unlock() }; return generation == expected }
    private static func shutdown(_ child: Process) {
        guard child.isRunning else { return }
        child.terminate()
        // Foundation reaps this exact child. The delayed bounded escalation
        // never waits on the UI thread and cannot target an unrelated process.
        DispatchQueue.global(qos: .utility).asyncAfter(deadline: .now() + 0.35) {
            if child.isRunning { kill(child.processIdentifier, SIGKILL) }
        }
    }
    deinit { readHandle?.readabilityHandler = nil; if let process { Self.shutdown(process) } }
}
