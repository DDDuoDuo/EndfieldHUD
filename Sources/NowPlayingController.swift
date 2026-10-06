import AppKit

enum NowPlayingSource: String, CaseIterable {
    case music, spotify, netease, qqMusic, kugou, system
    var bundleIdentifier: String {
        switch self {
        case .music: return "com.apple.Music"
        case .spotify: return "com.spotify.client"
        case .netease: return "com.netease.163music"
        case .qqMusic: return "com.tencent.QQMusicMac"
        case .kugou: return "com.kugou.mac.Music"
        case .system: return ""
        }
    }
    var title: String {
        switch self {
        case .music: return "Music"
        case .spotify: return "Spotify"
        case .netease: return "网易云音乐"
        case .qqMusic: return "QQ 音乐"
        case .kugou: return "酷狗音乐"
        case .system: return "Now Playing"
        }
    }
    var notificationNames: [String] {
        switch self {
        case .music: return ["com.apple.Music.playerInfo", "com.apple.iTunes.playerInfo"]
        case .spotify: return ["com.spotify.client.PlaybackStateChanged"]
        default: return []
        }
    }
}

struct NowPlayingApplication: Equatable {
    let source: NowPlayingSource
    let pid: Int32
    let bundleURL: URL
    let bundleIdentifier: String
    let displayName: String
    init(source: NowPlayingSource, pid: Int32, bundleURL: URL, bundleIdentifier: String? = nil, displayName: String? = nil) {
        self.source = source; self.pid = pid; self.bundleURL = bundleURL
        self.bundleIdentifier = bundleIdentifier ?? source.bundleIdentifier
        self.displayName = displayName ?? source.title
    }
}

struct NowPlayingTrack: Equatable {
    let title: String
    let artist: String
    let album: String
    let duration: Double?
    let position: Double?
    let isPlaying: Bool
    let sampledAt: TimeInterval
    let identifier: String?
    let timedLyrics: String?
    let artworkRevision: String?
    let supportsSeeking: Bool

    init(title: String, artist: String, album: String, duration: Double?, position: Double?,
         isPlaying: Bool, sampledAt: TimeInterval, identifier: String? = nil,
         timedLyrics: String? = nil, artworkRevision: String? = nil, supportsSeeking: Bool = true) {
        // Metadata is transient. Bound both retained strings and rendered text.
        self.title = String(title.prefix(512)); self.artist = String(artist.prefix(512)); self.album = String(album.prefix(512))
        let boundedDuration = duration.flatMap { $0.isFinite && $0 > 0 ? min($0, 604_800) : nil }
        self.duration = boundedDuration
        self.position = position.flatMap { $0.isFinite && $0 >= 0 ? min($0, boundedDuration ?? 604_800) : nil }
        self.isPlaying = isPlaying; self.sampledAt = sampledAt.isFinite ? sampledAt : 0
        self.identifier = identifier.flatMap { $0.isEmpty ? nil : String($0.prefix(512)) }
        self.timedLyrics = timedLyrics.flatMap { $0.utf8.count <= 512 * 1024 ? $0 : nil }
        self.artworkRevision = artworkRevision.map { String($0.prefix(256)) }
        self.supportsSeeking = supportsSeeking
    }

    func elapsed(at time: TimeInterval) -> Double? {
        guard let position else { return nil }
        let delta = isPlaying && time.isFinite ? max(0, time - sampledAt) : 0
        return min(duration ?? 604_800, position + delta)
    }

    /// Refresh timestamps, playback progress and pause state do not identify a
    /// new track. Keep seek gestures through those routine metadata updates.
    func hasSameIdentity(as other: NowPlayingTrack) -> Bool {
        if let identifier, let otherID = other.identifier, identifier != otherID { return false }
        return title == other.title && artist == other.artist && album == other.album && duration == other.duration
    }
}

enum NowPlayingFailure: Error, Equatable {
    case permissionRequired, permissionDenied, accessibilityRequired, unavailable, unsupported, timedOut, cancelled
    var message: String {
        switch self {
        case .permissionRequired: return L10n.text("Connect to allow playback controls.", "连接以允许播放控制。")
        case .permissionDenied: return L10n.text("Allow this player in System Settings > Privacy & Security > Automation, then refresh.", "请在系统设置 > 隐私与安全性 > 自动化中允许此播放器，然后刷新。")
        case .accessibilityRequired: return L10n.text("Allow Accessibility access to read this player's controls.", "请允许辅助功能访问以读取此播放器的控制。")
        case .unsupported: return L10n.text("This version of the player does not expose the required playback controls.", "此版本的播放器未提供所需的播放控制。")
        case .timedOut: return L10n.text("The player did not respond. Try refreshing.", "播放器未响应，请尝试刷新。")
        case .unavailable, .cancelled: return L10n.text("Play music in a supported player to connect automatically.", "在支持的播放器中播放音乐即可自动连接。")
        }
    }
}

enum NowPlayingCommand: Equatable { case playPause, previous, next, seek(Double) }
enum NowPlayingEvent {
    case playPause(NowPlayingSource), previous(NowPlayingSource), next(NowPlayingSource), seek(NowPlayingSource)
}

struct NowPlayingSnapshot: Equatable {
    var applications: [NowPlayingApplication] = []
    var application: NowPlayingApplication?
    var track: NowPlayingTrack?
    var failure: NowPlayingFailure?
    var busy = false
}

protocol NowPlayingBackend: AnyObject {
    func read(_ app: NowPlayingApplication, cancelled: () -> Bool) throws -> NowPlayingTrack?
    func perform(_ command: NowPlayingCommand, in app: NowPlayingApplication, cancelled: () -> Bool) throws
    func requestPermission(for app: NowPlayingApplication, cancelled: () -> Bool) throws
    func artwork(for track: NowPlayingTrack, in app: NowPlayingApplication, cancelled: () -> Bool) throws -> NowPlayingArtworkPayload?
    var awaitsAutomaticSnapshot: Bool { get }
    func automaticSnapshot(cancelled: () -> Bool) throws -> (NowPlayingApplication, NowPlayingTrack)?
    func subscribeToChanges(_ action: @escaping () -> Void) -> (() -> Void)
    func volume(in app: NowPlayingApplication, cancelled: () -> Bool) throws -> Double?
    func setVolume(_ value: Double, in app: NowPlayingApplication, cancelled: () -> Bool) throws
}

extension NowPlayingBackend {
    var awaitsAutomaticSnapshot: Bool { false }
    func artwork(for track: NowPlayingTrack, in app: NowPlayingApplication, cancelled: () -> Bool) throws -> NowPlayingArtworkPayload? { nil }
    func automaticSnapshot(cancelled: () -> Bool) throws -> (NowPlayingApplication, NowPlayingTrack)? { nil }
    func subscribeToChanges(_ action: @escaping () -> Void) -> (() -> Void) { {} }
    func volume(in app: NowPlayingApplication, cancelled: () -> Bool) throws -> Double? { nil }
    func setVolume(_ value: Double, in app: NowPlayingApplication, cancelled: () -> Bool) throws { throw NowPlayingFailure.unsupported }
}

private final class NowPlayingWorkToken {
    private let lock = NSLock()
    private var value = false
    func cancel() { lock.lock(); value = true; lock.unlock() }
    var cancelled: Bool { lock.lock(); defer { lock.unlock() }; return value }
}

/// Visible-only, event-driven service. No polling, subprocess, player launching,
/// or persisted listening history. Artwork shares its
/// serial worker and is separately coalesced, bounded and visibility-cancelled.
final class NowPlayingController {
    typealias Subscribe = (@escaping (NowPlayingSource?) -> Void) -> (() -> Void)
    private(set) var snapshot = NowPlayingSnapshot()
    private(set) var isRequestingPermission = false
    /// A warm snapshot may paint immediately, but only a completed current
    /// activation read can confirm that playback did not change while hidden.
    private(set) var hasFreshMetadataForPresentation = false
    var onEvent: ((NowPlayingEvent) -> Void)?
    private var observers: [UUID: () -> Void] = [:]
    private let backend: NowPlayingBackend
    private let applications: () -> [NowPlayingApplication]
    private let subscribe: Subscribe
    private let work: (@escaping () -> Void) -> Void
    private let deliver: (@escaping () -> Void) -> Void
    private var cancellation: (() -> Void)?
    private var backendCancellation: (() -> Void)?
    private var token: NowPlayingWorkToken?
    private var active = false
    private var inFlight = false
    private var pendingRefresh = false
    private var retryArtworkAfterRefresh = false
    private var selected: NowPlayingSource?
    private var preferred: NowPlayingSource?
    private var pendingCommand: (NowPlayingCommand, NowPlayingApplication, NowPlayingTrack)?
    private var pendingVolume: (Double, NowPlayingApplication)?
    private var generation = 0
    private let artworkFetch: NowPlayingArtworkLoader.Fetch
    private lazy var artworkLoader: NowPlayingArtworkLoader = {
        let backend = self.backend
        let loader = NowPlayingArtworkLoader(resolve: { app, track, cancelled in
            try backend.artwork(for: track, in: app, cancelled: cancelled)
        }, work: work, deliver: deliver, fetch: artworkFetch)
        loader.onChange = { [weak self] in self?.notifyObservers() }
        return loader
    }()
    var artworkImage: CGImage? { artworkLoader.image }
    private(set) var playerVolume: Double?
    private struct PendingSeek {
        let app: NowPlayingApplication
        let track: NowPlayingTrack
        let value: Double
        let sampledAt: TimeInterval
        let revision: Int
        var awaitingRead: Bool
    }
    private var seekRevision = 0
    private var optimisticSeek: PendingSeek?
    private var observedTrack: NowPlayingTrack?
    private let scheduleSeekRollback: (TimeInterval, @escaping () -> Void) -> (() -> Void)
    private var cancelSeekRollback: (() -> Void)?
    private let lyricsLoader: NowPlayingLyricsLoader
    private let catalog: NowPlayingCatalog?
    private var warmSnapshot: NowPlayingSnapshot?
    private var warmVolume: Double?
    var lyrics: NowPlayingLyrics? {
        if snapshot.track?.timedLyrics != nil, let embedded = lyricsLoader.lyrics { return embedded }
        return catalog?.lyrics ?? lyricsLoader.lyrics
    }


    init(backend: NowPlayingBackend = NowPlayingSystemBackend(),
         applications: (() -> [NowPlayingApplication])? = nil,
         subscribe: Subscribe? = nil,
         work: ((@escaping () -> Void) -> Void)? = nil,
         deliver: ((@escaping () -> Void) -> Void)? = nil,
         artworkFetch: @escaping NowPlayingArtworkLoader.Fetch = NowPlayingArtworkDownload.fetch,
         lyricsFetch: NowPlayingLyricsLoader.Fetch? = nil,
         scheduleSeekRollback: ((TimeInterval, @escaping () -> Void) -> (() -> Void))? = nil) {
        self.backend = backend; self.artworkFetch = artworkFetch
        self.catalog = backend is NowPlayingSystemBackend ? NowPlayingCatalog() : nil
        self.scheduleSeekRollback = scheduleSeekRollback ?? { delay, action in
            let item = DispatchWorkItem(block: action)
            DispatchQueue.main.asyncAfter(deadline: .now() + delay, execute: item)
            return { item.cancel() }
        }
        self.applications = applications ?? Self.runningApplications
        self.subscribe = subscribe ?? Self.subscribeToChanges
        let queue = DispatchQueue(label: "EndfieldHUD.NowPlaying", qos: .userInitiated)
        self.work = work ?? { queue.async(execute: $0) }
        self.deliver = deliver ?? { DispatchQueue.main.async(execute: $0) }
        // Injected fixture backends never make a live network request.
        self.lyricsLoader = NowPlayingLyricsLoader(fetch: lyricsFetch ?? (backend is NowPlayingSystemBackend
            ? NowPlayingLyricsDownload.fetch : { _, completion in completion(nil); return {} }))
        self.lyricsLoader.onChange = { [weak self] in self?.notifyObservers() }
        self.catalog?.onChange = { [weak self] in self?.catalogChanged() }
    }

    /// Explicit diagnostics only. The fake provider cannot send Apple Events,
    /// request permissions, launch a player or read a user's media library.
    static func fixture() -> NowPlayingController {
        let provider = NowPlayingFixtureBackend()
        let app = NowPlayingApplication(source: .music, pid: 424_242,
                                        bundleURL: URL(fileURLWithPath: "/EndfieldHUD-Fixture/Music.app"))
        return NowPlayingController(backend: provider, applications: { [app] }, subscribe: { _ in {} },
                                    work: { $0() }, deliver: { $0() })
    }

    @discardableResult func observe(_ action: @escaping () -> Void) -> UUID {
        let id = UUID(); observers[id] = action; return id
    }
    func removeObserver(_ id: UUID) { observers.removeValue(forKey: id) }

    func activate() {
        precondition(Thread.isMainThread)
        guard !active else { return }
        active = true; generation += 1; hasFreshMetadataForPresentation = false
        if let warm = warmSnapshot, let app = warm.application,
           isApplicationAlive(app, running: applications()) {
            snapshot = warm; snapshot.busy = false; observedTrack = warm.track; playerVolume = warmVolume
            changed()
        }
        cancellation = subscribe { [weak self] source in
            guard let self, self.active else { return }
            if let source { self.preferred = source }
            self.refresh()
        }
        backendCancellation = backend.subscribeToChanges { [weak self] in self?.refresh() }
        refresh()
    }

    func deactivate() {
        precondition(Thread.isMainThread)
        if snapshot.track != nil, snapshot.failure == nil || snapshot.failure == .timedOut || snapshot.failure == .unavailable {
            warmSnapshot = snapshot; warmSnapshot?.busy = false; warmSnapshot?.failure = nil
            warmSnapshot?.track = observedTrack ?? snapshot.track
            warmVolume = playerVolume
        }
        active = false; generation += 1; hasFreshMetadataForPresentation = false; pendingRefresh = false; isRequestingPermission = false; retryArtworkAfterRefresh = false
        pendingCommand = nil; pendingVolume = nil; clearOptimisticSeek(); observedTrack = nil
        token?.cancel(); cancellation?(); cancellation = nil; backendCancellation?(); backendCancellation = nil
        snapshot = NowPlayingSnapshot(); artworkLoader.clear(); lyricsLoader.clear(); catalog?.clear(); playerVolume = nil
    }

    deinit { token?.cancel(); cancellation?(); backendCancellation?(); cancelSeekRollback?() }

    func select(_ source: NowPlayingSource) {
        guard active, applications().contains(where: { $0.source == source }) else { return }
        selected = source; token?.cancel(); generation += 1; isRequestingPermission = false; retryArtworkAfterRefresh = false
        snapshot.application = nil; snapshot.track = nil; snapshot.failure = nil; clearOptimisticSeek(); observedTrack = nil
        artworkLoader.clear(); refresh()
    }

    func refreshManually() {
        precondition(Thread.isMainThread)
        guard active else { return }
        retryArtworkAfterRefresh = true
        refresh()
    }

    func refresh() {
        precondition(Thread.isMainThread)
        guard active else { return }
        if inFlight { pendingRefresh = true; return }
        let running = applications()
        if let selected, !running.contains(where: { $0.source == selected }) { self.selected = nil }
        snapshot.applications = running
        guard !running.isEmpty || backend is NowPlayingSystemBackend else {
            retryArtworkAfterRefresh = false
            snapshot.application = nil; snapshot.track = nil; snapshot.failure = .unavailable; snapshot.busy = false
            clearOptimisticSeek(); observedTrack = nil
            changed(); return
        }
        let candidates = selected.map { value in running.filter { $0.source == value } } ?? running
        let preferred = self.preferred
        let completedSeek = optimisticSeek.flatMap { $0.awaitingRead ? $0.revision : nil }
        begin { backend, cancelled in
            if let automatic = try? backend.automaticSnapshot(cancelled: cancelled) {
                return (automatic.0, Optional(automatic.1), Optional<NowPlayingFailure>.none,
                        try? backend.volume(in: automatic.0, cancelled: cancelled))
            }
            // The first native stream event arrives shortly after launch. Do
            // not hold it behind a full Accessibility scan of fallback apps.
            if backend.awaitsAutomaticSnapshot { throw NowPlayingFailure.timedOut }
            var results: [(NowPlayingApplication, NowPlayingTrack?, NowPlayingFailure?)] = []
            for app in candidates {
                guard !cancelled() else { throw NowPlayingFailure.cancelled }
                do { results.append((app, try backend.read(app, cancelled: cancelled), nil)) }
                catch { results.append((app, nil, (error as? NowPlayingFailure) ?? .unavailable)) }
            }
            guard !results.isEmpty else { throw NowPlayingFailure.unavailable }
            let result = results.first(where: { $0.0.source == preferred && $0.1?.isPlaying == true })
                ?? results.first(where: { $0.1?.isPlaying == true })
                ?? results.first(where: { $0.1 != nil }) ?? results[0]
            return (result.0, result.1, result.2, try? backend.volume(in: result.0, cancelled: cancelled))
        } completion: { [weak self] result in
            guard let self else { return }
            switch result {
            case .success(let value):
                if value.2 == nil {
                    self.hasFreshMetadataForPresentation = true
                    if value.1 == nil { self.warmSnapshot = nil; self.warmVolume = nil }
                }
                let retain = value.0 == self.snapshot.application && self.snapshot.track != nil
                    && (value.2 == .timedOut || value.2 == .unavailable)
                self.snapshot.application = value.0
                if !retain {
                    self.observedTrack = value.1
                    self.snapshot.track = value.1
                    if let seek = self.optimisticSeek {
                        let acknowledged = completedSeek == seek.revision && value.1.map { self.acknowledges($0, seek: seek) } == true
                        if seek.app != value.0 || value.1?.hasSameIdentity(as: seek.track) != true || acknowledged {
                            self.clearOptimisticSeek()
                        } else { self.applyOptimisticSeek() }
                    }
                }
                self.snapshot.failure = value.2
                self.playerVolume = self.pendingVolume.flatMap { $0.1 == value.0 ? $0.0 : nil }
                    ?? value.3.flatMap { $0.isFinite ? min(1, max(0, $0)) : nil }
            case .failure(let failure):
                if failure != .timedOut || self.snapshot.application.map({ app in self.isApplicationAlive(app, running: running) }) != true {
                    self.snapshot.track = nil; self.snapshot.application = nil; self.playerVolume = nil; self.clearOptimisticSeek(); self.observedTrack = nil
                }
                self.snapshot.failure = failure
            }
            if self.retryArtworkAfterRefresh {
                self.retryArtworkAfterRefresh = false
                if self.snapshot.failure == nil, let app = self.snapshot.application, let track = self.snapshot.track {
                    self.artworkLoader.invalidateMissing(application: app, track: track)
                    self.lyricsLoader.invalidateMissing(application: app, track: track)
                    self.catalog?.invalidateMissing(application: app, track: track)
                }
            }
        }
    }

    @discardableResult func setPlayerVolume(_ value: Double) -> Bool {
        guard active, value.isFinite, playerVolume != nil, let app = snapshot.application else { return false }
        let value = min(1, max(0, value))
        let previous = playerVolume; playerVolume = value
        if inFlight { pendingVolume = (value, app); notifyObservers(); return true }
        begin { backend, cancelled in try backend.setVolume(value, in: app, cancelled: cancelled) }
        completion: { [weak self] result in
            guard let self else { return }
            if let pending = self.pendingVolume, pending.1 == app { self.playerVolume = pending.0 }
            else if case .success = result { self.playerVolume = value }
            else { self.playerVolume = previous }
        }
        return true
    }

    func connect() {
        guard active, !inFlight, let app = snapshot.application else { return }
        isRequestingPermission = true
        begin { backend, cancelled in try backend.requestPermission(for: app, cancelled: cancelled); return () }
        completion: { [weak self] result in
            guard let self else { return }
            self.isRequestingPermission = false
            if case .failure(let failure) = result { self.snapshot.failure = failure }
            else { self.pendingRefresh = true }
        }
    }

    func perform(_ command: NowPlayingCommand) {
        guard active, snapshot.failure == nil, let app = snapshot.application, let track = snapshot.track else { return }
        var command = command
        if case .seek(let seconds) = command {
            guard seconds.isFinite, track.supportsSeeking, track.position != nil, let duration = track.duration else { return }
            let value = min(duration, max(0, seconds))
            command = .seek(value)
            cancelSeekRollback?(); cancelSeekRollback = nil
            seekRevision &+= 1
            optimisticSeek = PendingSeek(app: app, track: track, value: value,
                sampledAt: ProcessInfo.processInfo.systemUptime, revision: seekRevision, awaitingRead: false)
            applyOptimisticSeek()
        } else if inFlight, let pending = pendingCommand, case .seek = pending.0 {
            // A later play/skip replaces a queued seek that will never be sent.
            // Do not leave that unsent position visible indefinitely.
            clearOptimisticSeek(); snapshot.track = observedTrack
        }
        if inFlight { pendingCommand = (command, app, track); notifyObservers(); return }
        let commandSeekRevision = optimisticSeek?.revision
        begin { backend, cancelled in try backend.perform(command, in: app, cancelled: cancelled); return () }
        completion: { [weak self] result in
            guard let self else { return }
            if case .failure(let failure) = result {
                self.snapshot.failure = failure
                if self.optimisticSeek?.revision == commandSeekRevision {
                    self.clearOptimisticSeek(); self.snapshot.track = self.observedTrack
                }
            } else {
                if case .seek = command, self.optimisticSeek?.revision == commandSeekRevision {
                    self.optimisticSeek?.awaitingRead = true
                    self.armSeekRollback()
                }
                switch command {
                case .playPause: self.onEvent?(.playPause(app.source))
                case .previous: self.onEvent?(.previous(app.source))
                case .next: self.onEvent?(.next(app.source))
                case .seek: self.onEvent?(.seek(app.source))
                }
                self.pendingRefresh = true
            }
        }
    }

    private func begin<T>(_ action: @escaping (NowPlayingBackend, @escaping () -> Bool) throws -> T,
                          completion: @escaping (Result<T, NowPlayingFailure>) -> Void) {
        inFlight = true; snapshot.busy = true; changed()
        let token = NowPlayingWorkToken(); self.token = token
        let generation = self.generation, backend = self.backend, deliver = self.deliver
        work { [weak self] in
            let result: Result<T, NowPlayingFailure>
            do { result = .success(try action(backend, { token.cancelled })) }
            catch { result = .failure((error as? NowPlayingFailure) ?? .unavailable) }
            deliver { [weak self] in
                guard let self else { return }
                self.inFlight = false
                if self.active, self.generation == generation, !token.cancelled {
                    self.snapshot.busy = false; completion(result); self.changed()
                }
                self.drainPending()
            }
        }
    }

    private func drainPending() {
        guard active, !inFlight else { return }
        if let pending = pendingCommand {
            pendingCommand = nil
            if snapshot.application == pending.1, let track = snapshot.track {
                let seek: Bool; if case .seek = pending.0 { seek = true } else { seek = false }
                if !seek || track.hasSameIdentity(as: pending.2) { perform(pending.0); if inFlight { return } }
            }
        }
        if let pending = pendingVolume {
            pendingVolume = nil
            if snapshot.application == pending.1 { _ = setPlayerVolume(pending.0); if inFlight { return } }
        }
        if pendingRefresh { pendingRefresh = false; refresh() }
    }

    private func clearOptimisticSeek() {
        optimisticSeek = nil; cancelSeekRollback?(); cancelSeekRollback = nil
    }
    private func acknowledges(_ track: NowPlayingTrack, seek: PendingSeek) -> Bool {
        guard let position = track.position else { return false }
        // A successful command exit is not a stream acknowledgement. Some
        // players deliver the new timestamp after the first post-command read.
        let advancement = track.isPlaying ? min(3, max(0, track.sampledAt - seek.sampledAt)) : 0
        let expected = min(track.duration ?? 604_800, seek.value + advancement)
        return abs(position - expected) <= 1.25
    }
    private func armSeekRollback() {
        guard let seek = optimisticSeek else { return }
        cancelSeekRollback?()
        cancelSeekRollback = scheduleSeekRollback(2.5) { [weak self] in
            guard let self, self.active, self.optimisticSeek?.revision == seek.revision else { return }
            self.clearOptimisticSeek(); self.snapshot.track = self.observedTrack
            self.changed()
        }
    }

    private func applyOptimisticSeek() {
        guard let seek = optimisticSeek, let current = snapshot.track,
              snapshot.application == seek.app, current.hasSameIdentity(as: seek.track) else { return }
        snapshot.track = NowPlayingTrack(title: current.title, artist: current.artist, album: current.album,
            duration: current.duration, position: seek.value, isPlaying: current.isPlaying, sampledAt: seek.sampledAt,
            identifier: current.identifier, timedLyrics: current.timedLyrics, artworkRevision: current.artworkRevision,
            supportsSeeking: current.supportsSeeking)
    }

    private func changed() {
        if active, let app = snapshot.application, let track = snapshot.track {
            catalog?.request(application: app, track: track)
            artworkLoader.request(application: app, track: track)
            if track.timedLyrics != nil { lyricsLoader.request(application: app, track: track) }
            else if catalog?.isLoading != true && catalog?.lyrics == nil { lyricsLoader.request(application: app, track: track) }
            else { lyricsLoader.clear() }
        } else { artworkLoader.clear(); lyricsLoader.clear(); catalog?.clear() }
        notifyObservers()
    }
    private func catalogChanged() {
        guard active, let app = snapshot.application, let track = snapshot.track else { return }
        if let url = catalog?.artwork { artworkLoader.provideArtworkURL(url, application: app, track: track) }
        if track.timedLyrics != nil { lyricsLoader.request(application: app, track: track) }
        else if catalog?.isLoading == false, catalog?.lyrics == nil { lyricsLoader.request(application: app, track: track) }
        else if catalog?.lyrics != nil { lyricsLoader.clear() }
        notifyObservers()
    }
    private func notifyObservers() { for observer in Array(observers.values) { observer() } }

    private func isApplicationAlive(_ app: NowPlayingApplication, running: [NowPlayingApplication]) -> Bool {
        if running.contains(app) { return true }
        guard backend is NowPlayingSystemBackend, let process = NSRunningApplication(processIdentifier: app.pid) else { return false }
        return !process.isTerminated && process.bundleIdentifier == app.bundleIdentifier && process.bundleURL == app.bundleURL
    }

    private static func runningApplications() -> [NowPlayingApplication] {
        NSWorkspace.shared.runningApplications.compactMap { app in
            guard !app.isTerminated, let id = app.bundleIdentifier,
                  let source = NowPlayingSource.allCases.first(where: { $0.bundleIdentifier == id }),
                  let url = app.bundleURL else { return nil }
            return NowPlayingApplication(source: source, pid: app.processIdentifier, bundleURL: url)
        }.sorted { $0.source.rawValue < $1.source.rawValue }
    }

    private static func subscribeToChanges(_ action: @escaping (NowPlayingSource?) -> Void) -> () -> Void {
        let distributed = DistributedNotificationCenter.default()
        var tokens: [NSObjectProtocol] = []
        for source in NowPlayingSource.allCases {
            for name in source.notificationNames {
                tokens.append(distributed.addObserver(forName: .init(name), object: nil, queue: .main) { _ in action(source) })
            }
        }
        let workspace = NSWorkspace.shared.notificationCenter
        let local = [NSWorkspace.didLaunchApplicationNotification, NSWorkspace.didTerminateApplicationNotification,
                     NSWorkspace.didActivateApplicationNotification].map { name in
            workspace.addObserver(forName: name, object: nil, queue: .main) { notification in
                guard let app = notification.userInfo?[NSWorkspace.applicationUserInfoKey] as? NSRunningApplication,
                      NowPlayingSource.allCases.contains(where: { $0.bundleIdentifier == app.bundleIdentifier }) else { return }
                action(nil)
            }
        }
        return { tokens.forEach(distributed.removeObserver); local.forEach(workspace.removeObserver) }
    }
}

private final class NowPlayingFixtureBackend: NowPlayingBackend {
    private var playing = false
    private var position: Double = 42
    private var trackNumber = 1
    private var sampledAt = ProcessInfo.processInfo.systemUptime
    func read(_ app: NowPlayingApplication, cancelled: () -> Bool) throws -> NowPlayingTrack? {
        guard !cancelled() else { throw NowPlayingFailure.cancelled }
        return NowPlayingTrack(title: "EndfieldHUD Fixture \(trackNumber)", artist: "Local verification", album: "Now Playing",
                               duration: 210, position: currentPosition, isPlaying: playing, sampledAt: ProcessInfo.processInfo.systemUptime,
                               timedLyrics: "[00:00]Previous\n[00:30]Current\n[01:00]Next")
    }
    private var currentPosition: Double { min(210, position + (playing ? max(0, ProcessInfo.processInfo.systemUptime - sampledAt) : 0)) }
    func perform(_ command: NowPlayingCommand, in app: NowPlayingApplication, cancelled: () -> Bool) throws {
        guard !cancelled() else { throw NowPlayingFailure.cancelled }
        position = currentPosition; sampledAt = ProcessInfo.processInfo.systemUptime
        switch command {
        case .playPause: playing.toggle()
        case .previous: trackNumber = max(1, trackNumber - 1); position = 0
        case .next: trackNumber += 1; position = 0
        case .seek(let value): position = min(210, max(0, value))
        }
    }
    private static let cover = NowPlayingArtworkLoader.fixturePNG()
    func artwork(for track: NowPlayingTrack, in app: NowPlayingApplication, cancelled: () -> Bool) throws -> NowPlayingArtworkPayload? {
        guard !cancelled() else { throw NowPlayingFailure.cancelled }
        return .embedded(Self.cover)
    }
    func requestPermission(for app: NowPlayingApplication, cancelled: () -> Bool) throws {}
}
