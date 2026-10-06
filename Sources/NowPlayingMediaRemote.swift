import AppKit
import Darwin
import ObjectiveC

/// Optional, dynamically loaded system metadata bridge. This private API may
/// return no data on newer macOS; the caller then uses supported player APIs.
/// This in-process fallback is complemented by the separately scoped adapter.
final class NowPlayingMediaRemote {
    private typealias GetInfo = @convention(c) (DispatchQueue, @escaping @convention(block) (CFDictionary?) -> Void) -> Void
    private typealias GetPID = @convention(c) (DispatchQueue, @escaping @convention(block) (Int32) -> Void) -> Void
    private typealias Register = @convention(c) (DispatchQueue) -> Void
    private typealias Unregister = @convention(c) () -> Void
    private typealias Send = @convention(c) (Int32, CFDictionary?) -> Bool
    private typealias Seek = @convention(c) (Double) -> Void
    private static let handle = dlopen("/System/Library/PrivateFrameworks/MediaRemote.framework/MediaRemote", RTLD_LAZY | RTLD_LOCAL)
    private var lastApplication: NowPlayingApplication?
    private var lastTrack: NowPlayingTrack?
    private var artwork: Data?
    private var artworkRevision = 0

    private static func symbol<T>(_ name: String, _: T.Type) -> T? {
        guard let handle, let value = dlsym(handle, name) else { return nil }
        return unsafeBitCast(value, to: T.self)
    }
    private static func object(_ target: AnyObject?, _ name: String) -> AnyObject? {
        guard let target, let type = object_getClass(target),
              let method = class_getInstanceMethod(type, NSSelectorFromString(name)),
              method_getNumberOfArguments(method) == 2 else { return nil }
        let encoding = method_copyReturnType(method)
        defer { free(encoding) }
        guard encoding.pointee == 64 else { return nil } // Only object-returning selectors.
        typealias Getter = @convention(c) (AnyObject, Selector) -> Unmanaged<AnyObject>?
        let getter = unsafeBitCast(method_getImplementation(method), to: Getter.self)
        return getter(target, NSSelectorFromString(name))?.takeUnretainedValue()
    }
    private static func localClient() -> AnyObject? {
        guard let request = NSClassFromString("MRNowPlayingRequest") else { return nil }
        return object(object(request as AnyObject, "localNowPlayingPlayerPath"), "client")
    }
    private static func currentBundle() -> String? {
        object(localClient(), "bundleIdentifier") as? String
    }

    func read(cancelled: () -> Bool) throws -> (NowPlayingApplication, NowPlayingTrack)? {
        guard !cancelled() else { throw NowPlayingFailure.cancelled }
        var info: [String: Any]?
        if let request = NSClassFromString("MRNowPlayingRequest"),
           let item = Self.object(request as AnyObject, "localNowPlayingItem") {
            info = Self.object(item, "nowPlayingInfo") as? [String: Any]
        }
        var pid: Int32 = 0
        let bundle = Self.currentBundle()
        if let bundle { pid = NSRunningApplication.runningApplications(withBundleIdentifier: bundle).first?.processIdentifier ?? 0 }
        if info?.isEmpty != false, let get = Self.symbol("MRMediaRemoteGetNowPlayingInfo", GetInfo.self) {
            let answer = NowPlayingRemoteAnswer()
            let group = DispatchGroup(); group.enter()
            get(.global(qos: .utility)) { value in answer.setInfo(value as? [String: Any]); group.leave() }
            if pid == 0, let getPID = Self.symbol("MRMediaRemoteGetNowPlayingApplicationPID", GetPID.self) {
                group.enter(); getPID(.global(qos: .utility)) { value in answer.setPID(value); group.leave() }
            }
            guard group.wait(timeout: .now() + 1.2) == .success else { throw NowPlayingFailure.timedOut }
            info = answer.info; if pid == 0 { pid = answer.pid }
        }
        guard !cancelled() else { throw NowPlayingFailure.cancelled }
        guard let info, let running = NSRunningApplication(processIdentifier: pid), !running.isTerminated,
              let id = running.bundleIdentifier, let url = running.bundleURL,
              let parsed = Self.track(from: info) else { return nil }
        let application = NowPlayingApplication(source: NowPlayingSource.allCases.first { $0.bundleIdentifier == id } ?? .system,
            pid: pid, bundleURL: url, bundleIdentifier: id, displayName: running.localizedName)
        let data = info["kMRMediaRemoteNowPlayingInfoArtworkData"] as? Data
        if data != artwork { artworkRevision &+= 1; artwork = data.flatMap { $0.count <= NowPlayingArtworkLoader.maximumBytes ? $0 : nil } }
        let track = NowPlayingTrack(title: parsed.title, artist: parsed.artist, album: parsed.album,
            duration: parsed.duration, position: parsed.position, isPlaying: parsed.isPlaying, sampledAt: parsed.sampledAt,
            identifier: parsed.identifier, timedLyrics: parsed.timedLyrics, artworkRevision: artwork.map { _ in String(artworkRevision) })
        lastApplication = application; lastTrack = track
        return (application, track)
    }

    static func track(from info: [String: Any], now: TimeInterval = ProcessInfo.processInfo.systemUptime,
                      date: Date = Date()) -> NowPlayingTrack? {
        func string(_ key: String) -> String? { info["kMRMediaRemoteNowPlayingInfo" + key] as? String }
        func number(_ key: String) -> Double? { (info["kMRMediaRemoteNowPlayingInfo" + key] as? NSNumber)?.doubleValue }
        guard let title = string("Title"), !title.isEmpty else { return nil }
        let playing = (number("PlaybackRate") ?? 0) > 0
        var position = number("ElapsedTime")
        if playing, let timestamp = info["kMRMediaRemoteNowPlayingInfoTimestamp"] as? Date, let value = position {
            let delta = date.timeIntervalSince(timestamp)
            if delta.isFinite, delta >= 0, delta <= 604_800 { position = value + delta * min(4, number("PlaybackRate") ?? 1) }
        }
        return NowPlayingTrack(title: title, artist: string("Artist") ?? "", album: string("Album") ?? "",
            duration: number("Duration"), position: position, isPlaying: playing, sampledAt: now,
            identifier: string("UniqueIdentifier"), timedLyrics: string("Lyrics"))
    }

    func payload(for track: NowPlayingTrack, in app: NowPlayingApplication) -> NowPlayingArtworkPayload? {
        guard app == lastApplication, lastTrack?.hasSameIdentity(as: track) == true, let artwork else { return nil }
        return .embedded(artwork)
    }
    func owns(_ application: NowPlayingApplication) -> Bool {
        guard let running = NSRunningApplication(processIdentifier: application.pid), !running.isTerminated,
              running.bundleIdentifier == application.bundleIdentifier else { return false }
        return Self.currentBundle() == application.bundleIdentifier
    }
    func perform(_ command: NowPlayingCommand, in application: NowPlayingApplication) throws {
        guard owns(application) else { throw NowPlayingFailure.unavailable }
        if case .seek(let value) = command {
            guard value.isFinite, value >= 0, let seek = Self.symbol("MRMediaRemoteSetElapsedTime", Seek.self) else { throw NowPlayingFailure.unsupported }
            seek(value); return
        }
        guard let send = Self.symbol("MRMediaRemoteSendCommand", Send.self) else { throw NowPlayingFailure.unsupported }
        let code: Int32
        switch command { case .playPause: code = 2; case .next: code = 4; default: code = 5 }
        guard send(code, nil) else { throw NowPlayingFailure.unavailable }
    }
    func subscribe(_ action: @escaping () -> Void) -> (() -> Void) {
        guard let register = Self.symbol("MRMediaRemoteRegisterForNowPlayingNotifications", Register.self) else { return {} }
        register(.main)
        let center = NotificationCenter.default
        let names = ["kMRMediaRemoteNowPlayingInfoDidChangeNotification", "kMRMediaRemoteNowPlayingApplicationDidChangeNotification",
                     "kMRMediaRemoteNowPlayingApplicationIsPlayingDidChangeNotification", "kMRMediaRemoteNowPlayingPlaybackQueueDidChangeNotification"]
        var observed = Set<String>()
        for name in names {
            observed.insert(name); observed.insert(String(name.dropFirst()))
            if let handle = Self.handle, let address = dlsym(handle, name) {
                let value = address.assumingMemoryBound(to: Optional<CFString>.self).pointee
                if let value { observed.insert(value as String) }
            }
        }
        let coalescer = NowPlayingNotificationCoalescer(action: action)
        let tokens = observed.map { name in center.addObserver(forName: .init(name), object: nil, queue: .main) { _ in coalescer.signal() } }
        return {
            coalescer.cancel(); tokens.forEach(center.removeObserver)
            Self.symbol("MRMediaRemoteUnregisterForNowPlayingNotifications", Unregister.self)?()
        }
    }
}

private final class NowPlayingRemoteAnswer {
    private let lock = NSLock()
    private var value: [String: Any]?
    private var process: Int32 = 0
    func setInfo(_ value: [String: Any]?) { lock.lock(); self.value = value; lock.unlock() }
    func setPID(_ value: Int32) { lock.lock(); process = value; lock.unlock() }
    var info: [String: Any]? { lock.lock(); defer { lock.unlock() }; return value }
    var pid: Int32 { lock.lock(); defer { lock.unlock() }; return process }
}

final class NowPlayingNotificationCoalescer {
    private let action: () -> Void
    private var pending: DispatchWorkItem?
    private var active = true
    init(action: @escaping () -> Void) { self.action = action }
    func signal() {
        guard active, pending == nil else { return }
        let item = DispatchWorkItem { [weak self] in
            guard let self, self.active else { return }; self.pending = nil; self.action()
        }
        pending = item; DispatchQueue.main.asyncAfter(deadline: .now() + 0.12, execute: item)
    }
    func cancel() { active = false; pending?.cancel(); pending = nil }
    deinit { pending?.cancel() }
}

final class NowPlayingSystemBackend: NowPlayingBackend {
    var awaitsAutomaticSnapshot: Bool { adapter.awaitsSnapshot }
    private let media = NowPlayingMediaRemote()
    private let adapter = NowPlayingAdapter()
    private let apple = NowPlayingAppleEvents()
    private let accessibility = NowPlayingAccessibility()
    func automaticSnapshot(cancelled: () -> Bool) throws -> (NowPlayingApplication, NowPlayingTrack)? {
        guard !cancelled() else { throw NowPlayingFailure.cancelled }
        if let snapshot = adapter.snapshot() { return snapshot }
        return adapter.isAvailable ? nil : try media.read(cancelled: cancelled)
    }
    func read(_ app: NowPlayingApplication, cancelled: () -> Bool) throws -> NowPlayingTrack? {
        if app.source == .music || app.source == .spotify { return try apple.read(app, cancelled: cancelled) }
        return try accessibility.read(app, cancelled: cancelled)
    }
    func artwork(for track: NowPlayingTrack, in app: NowPlayingApplication, cancelled: () -> Bool) throws -> NowPlayingArtworkPayload? {
        if let payload = adapter.artwork(for: track, in: app) ?? media.payload(for: track, in: app) { return payload }
        if app.source == .music || app.source == .spotify { return try apple.artwork(for: track, in: app, cancelled: cancelled) }
        return accessibility.artwork(for: track, in: app)
    }
    func perform(_ command: NowPlayingCommand, in app: NowPlayingApplication, cancelled: () -> Bool) throws {
        guard !cancelled() else { throw NowPlayingFailure.cancelled }
        if adapter.snapshot()?.0 == app { try adapter.perform(command, in: app, cancelled: cancelled); return }
        if media.owns(app), (try? media.perform(command, in: app)) != nil { return }
        if app.source == .music || app.source == .spotify { try apple.perform(command, in: app, cancelled: cancelled) }
        else { try accessibility.perform(command, in: app, cancelled: cancelled) }
    }
    func requestPermission(for app: NowPlayingApplication, cancelled: () -> Bool) throws {
        if app.source == .music || app.source == .spotify { try apple.requestPermission(for: app, cancelled: cancelled) }
        else { throw NowPlayingFailure.accessibilityRequired }
    }
    func volume(in app: NowPlayingApplication, cancelled: () -> Bool) throws -> Double? {
        if app.source == .music || app.source == .spotify { return try apple.volume(in: app, cancelled: cancelled) }
        return try accessibility.volume(in: app, cancelled: cancelled)
    }
    func setVolume(_ value: Double, in app: NowPlayingApplication, cancelled: () -> Bool) throws {
        if app.source == .music || app.source == .spotify { try apple.setVolume(value, in: app, cancelled: cancelled) }
        else { try accessibility.setVolume(value, in: app, cancelled: cancelled) }
    }
    func subscribeToChanges(_ action: @escaping () -> Void) -> (() -> Void) {
        let cancelMedia = adapter.isAvailable ? adapter.start(action) : media.subscribe(action)
        let cancelAX = accessibility.subscribe(action)
        return { cancelMedia(); cancelAX() }
    }
}
