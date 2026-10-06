import Foundation

enum NowPlayingAutomaticTests {
    static func run() -> Int {
        var count = 0
        func check(_ condition: Bool, _ message: String) { count += 1; if !condition { fatalError(message) } }
        let music = NowPlayingApplication(source: .music, pid: 11, bundleURL: URL(fileURLWithPath: "/Music.app"))
        let netease = NowPlayingApplication(source: .netease, pid: 22, bundleURL: URL(fileURLWithPath: "/NetEase.app"))
        let track = NowPlayingTrack(title: "Fixture", artist: "Artist", album: "Album", duration: 100, position: 5, isPlaying: true, sampledAt: 10)
        let backend = Backend(); backend.automatic = (netease, track)
        var jobs: [() -> Void] = []
        let controller = NowPlayingController(backend: backend, applications: { [music, netease] }, subscribe: { _ in {} },
            work: { jobs.append($0) }, deliver: { $0() })
        func drain() { var n = 0; while !jobs.isEmpty { n += 1; precondition(n < 100); jobs.removeFirst()() } }
        controller.activate(); drain()
        check(controller.snapshot.application == netease && controller.snapshot.track == track && backend.fallbackReads == 0,
              "The system's active publisher wins automatically without manual source selection")
        check(controller.playerVolume == 0.5, "The controller exposes a real player volume capability")
        controller.refresh()
        _ = controller.setPlayerVolume(0.2); _ = controller.setPlayerVolume(0.3); _ = controller.setPlayerVolume(0.85)
        controller.perform(.seek(45)); controller.perform(.seek(75))
        check(controller.snapshot.track?.position == 75, "A queued seek immediately updates the shared rail, lyrics, and accessibility model")
        jobs.removeFirst()()
        check(controller.snapshot.track?.position == 75, "A pre-command read cannot snap the optimistic seek back")
        check(controller.playerVolume == 0.85, "Dragging while a read is busy keeps the newest optimistic value")
        drain()
        check(backend.volumes == [0.85] && backend.commands == [.seek(75)],
              "Busy background refresh coalesces the latest volume and seek instead of losing their final values")
        check(backend.operations.firstIndex(of: "command")! < backend.operations.firstIndex(of: "volume")!,
              "A queued user playback command takes priority over subsequent background refreshes")
        controller.refresh(); controller.perform(.seek(90))
        backend.automatic = (netease, NowPlayingTrack(title: "New track", artist: "Artist", album: "", duration: 200,
            position: 0, isPlaying: true, sampledAt: 20))
        drain()
        check(backend.commands == [.seek(75)], "A queued seek cannot accidentally seek a different song")
        controller.deactivate()
        check(backend.cancellations == 1 && controller.snapshot.track == nil && controller.playerVolume == nil,
              "Leaving removes backend observation and transient control state")

        let seekBackend = Backend()
        func seekTrack(_ position: Double) -> NowPlayingTrack {
            NowPlayingTrack(title: "Seek fixture", artist: "Artist", album: "", duration: 120,
                position: position, isPlaying: false, sampledAt: 0)
        }
        seekBackend.automatic = (netease, seekTrack(5))
        var seekJobs: [() -> Void] = [], deadlines: [() -> Void] = []
        var deadlineCancellations = 0
        let seeking = NowPlayingController(backend: seekBackend, applications: { [netease] }, subscribe: { _ in {} },
            work: { seekJobs.append($0) }, deliver: { $0() }, scheduleSeekRollback: { delay, action in
                check(delay == 2.5, "A seek has one bounded rollback deadline")
                deadlines.append(action); return { deadlineCancellations += 1 }
            })
        func drainSeeking() { var n = 0; while !seekJobs.isEmpty { n += 1; precondition(n < 100); seekJobs.removeFirst()() } }
        seeking.activate(); drainSeeking(); seeking.perform(.seek(75)); drainSeeking()
        check(seeking.snapshot.track?.position == 75, "A cached old stream value after helper exit cannot acknowledge a seek")
        seekBackend.automatic = (netease, seekTrack(75)); seeking.refresh(); drainSeeking()
        check(seeking.snapshot.track?.position == 75 && deadlineCancellations == 1,
              "A plausible same-track position acknowledges the seek and cancels its finite deadline")
        deadlines[0]()
        check(seeking.snapshot.track?.position == 75, "A stale deadline cannot roll back an acknowledged seek")
        seeking.perform(.seek(90)); drainSeeking()
        check(seeking.snapshot.track?.position == 90, "A second unacknowledged target stays optimistic")
        let beforeTimeoutCommands = seekBackend.commands.count
        deadlines.last?()
        check(seeking.snapshot.track?.position == 75 && seekBackend.commands.count == beforeTimeoutCommands,
              "An ignored seek rolls back once without polling or resending hidden commands")
        seeking.refresh(); seeking.perform(.seek(40)); seeking.perform(.playPause)
        check(seeking.snapshot.track?.position == 75, "Replacing an unsent queued seek immediately rolls back its target")
        drainSeeking()
        check(!seekBackend.commands.contains(.seek(40)) && seekBackend.commands.last == .playPause,
              "A superseded seek is never sent after a queued playback command")
        seeking.perform(.seek(50)); drainSeeking(); let hiddenDeadline = deadlines.last!
        seeking.deactivate(); hiddenDeadline()
        check(seeking.snapshot.track == nil, "Hiding cancels the finite rollback and stale callbacks cannot restore content")

        seeking.activate()
        check(seeking.snapshot.track != nil && !seekJobs.isEmpty,
              "Reopening restores the bounded warm metadata snapshot before asynchronous backend work")
        check(!seeking.hasFreshMetadataForPresentation, "Warm cached rendering is not reported as fresh player metadata")
        seekBackend.automatic = (netease, NowPlayingTrack(title: "Changed while hidden", artist: "Artist", album: "",
            duration: 200, position: 10, isPlaying: false, sampledAt: 0))
        drainSeeking()
        check(seeking.hasFreshMetadataForPresentation && seeking.snapshot.track?.title == "Changed while hidden",
              "The first presentation refresh replaces a warm track that changed while away")
        let subscriptionsBefore = seekBackend.subscriptions
        seeking.activate()
        check(seekBackend.subscriptions == subscriptionsBefore, "Repeated presentation preparation cannot start duplicate subscriptions")
        seeking.deactivate()
        check(!seeking.hasFreshMetadataForPresentation, "Hidden presentation state cannot claim a current player read")
        seekBackend.automatic = nil
        seeking.activate(); drainSeeking(); seeking.deactivate(); seeking.activate()
        check(seeking.snapshot.track == nil, "A confirmed empty player clears the warm snapshot instead of resurrecting an old song")
        drainSeeking(); seeking.deactivate()

        let starting = Backend(); starting.waitingForStream = true
        let cold = NowPlayingController(backend: starting, applications: { [netease] }, subscribe: { _ in {} }, work: { $0() }, deliver: { $0() })
        cold.activate()
        check(starting.fallbackReads == 0, "Cold stream startup is not blocked behind an Accessibility fallback scan")
        starting.waitingForStream = false; starting.automatic = (netease, track); cold.refresh()
        check(cold.snapshot.track != nil && starting.fallbackReads == 0, "The first stream event immediately replaces the short connecting state")
        cold.deactivate()

        let data: [String: Any] = ["kMRMediaRemoteNowPlayingInfoTitle": "Fixture", "kMRMediaRemoteNowPlayingInfoArtist": "Artist",
            "kMRMediaRemoteNowPlayingInfoDuration": 100, "kMRMediaRemoteNowPlayingInfoElapsedTime": 20,
            "kMRMediaRemoteNowPlayingInfoPlaybackRate": 1, "kMRMediaRemoteNowPlayingInfoTimestamp": Date(timeIntervalSince1970: 998)]
        let remote = NowPlayingMediaRemote.track(from: data, now: 10, date: Date(timeIntervalSince1970: 1000))!
        check(remote.position == 22 && remote.elapsed(at: 13) == 25, "Native MediaRemote timestamps interpolate locally between notifications")
        check(NowPlayingMediaRemote.track(from: [:]) == nil, "An empty system payload does not fabricate a playing track")
        check(NowPlayingAccessibility.clock("03:45 ") == 225 && NowPlayingAccessibility.clock("1:02:03") == 3723,
              "Accessible player times parse finite minutes and hours")
        check(NowPlayingAccessibility.clock("artist: title") == nil && NowPlayingAccessibility.clock("00:99") == nil,
              "Player time parsing ignores unrelated text and invalid timestamps")
        check(NowPlayingAccessibility.track(heading: "Fixture - Artist", position: 20, duration: 100, playing: false)?.supportsSeeking == false,
              "The accessibility fallback does not claim an unavailable seek control")
        check(NowPlayingAccessibility.isVolumeLabel("Volume") && NowPlayingAccessibility.isVolumeLabel("音量")
              && !NowPlayingAccessibility.isVolumeLabel("Playback position"), "Only an explicitly named volume slider can become native app volume")

        func message(_ payload: [String: Any], diff: Bool = false) -> Data {
            try! JSONSerialization.data(withJSONObject: ["type": "data", "diff": diff, "payload": payload])
        }
        var stream = NowPlayingAdapterState()
        let cover = NowPlayingArtworkLoader.fixturePNG()
        let payload: [String: Any] = ["title": "Fixture", "artist": "Artist", "album": "Album", "bundleIdentifier": netease.bundleIdentifier,
            "processIdentifier": 22, "playing": true, "durationMicros": 100_000_000, "elapsedTimeMicros": 20_000_000,
            "timestampEpochMicros": 999_000_000, "artworkData": cover.base64EncodedString()]
        check(stream.accept(message(payload), date: Date(timeIntervalSince1970: 1000)), "A full adapter payload is accepted")
        check(stream.track(now: 10, date: Date(timeIntervalSince1970: 1000))?.position == 21,
              "Microsecond adapter timing is converted exactly once before local interpolation")
        check(stream.artwork == cover && stream.payload["artworkData"] == nil,
              "The adapter retains one bounded compressed cover rather than duplicate base64 data")
        check(!stream.accept(message(payload), date: Date(timeIntervalSince1970: 1000)), "Identical full payloads do not redraw the panel")
        check(stream.accept(message(["elapsedTimeMicros": 30_000_000], diff: true)) && stream.artwork == cover,
              "Small progress diffs preserve album art and track identity")
        check(!stream.accept(message([:], diff: true)), "Empty diffs do not emit another UI update")
        check(stream.accept(message(["title": "Other", "playing": false, "bundleIdentifier": netease.bundleIdentifier]))
              && stream.artwork == nil, "A new full track cannot inherit a previous album cover")
        check(stream.clear() && !stream.received && stream.track() == nil && stream.artwork == nil,
              "Unexpected stream EOF clears metadata and stale artwork for fallback")
        check(!stream.clear(), "Repeated stream teardown is idempotent")
        check(stream.accept(message([:])) && stream.track() == nil, "An explicit empty full payload clears the current player")
        check(!stream.accept(Data("broken".utf8)), "Malformed helper output is rejected")
        check(!stream.accept(Data(repeating: 65, count: NowPlayingAdapterState.maximumLineBytes + 1)),
              "The adapter rejects unbounded lines before JSON parsing")
        let a = NowPlayingMediaRemote.track(from: ["kMRMediaRemoteNowPlayingInfoTitle": "Same", "kMRMediaRemoteNowPlayingInfoContentItemIdentifier": "ephemeral1"])!
        let b = NowPlayingMediaRemote.track(from: ["kMRMediaRemoteNowPlayingInfoTitle": "Same", "kMRMediaRemoteNowPlayingInfoContentItemIdentifier": "ephemeral2"])!
        check(a.hasSameIdentity(as: b), "NetEase transport-item UUID changes do not masquerade as a new song")
        return count
    }
    private final class Backend: NowPlayingBackend {
        var automatic: (NowPlayingApplication, NowPlayingTrack)?
        var waitingForStream = false
        var awaitsAutomaticSnapshot: Bool { waitingForStream }
        var fallbackReads = 0, cancellations = 0, subscriptions = 0
        var volumes: [Double] = [], commands: [NowPlayingCommand] = [], operations: [String] = []
        var currentVolume = 0.5
        func automaticSnapshot(cancelled: () -> Bool) throws -> (NowPlayingApplication, NowPlayingTrack)? { operations.append("read"); return automatic }
        func read(_ app: NowPlayingApplication, cancelled: () -> Bool) throws -> NowPlayingTrack? { fallbackReads += 1; return nil }
        func perform(_ command: NowPlayingCommand, in app: NowPlayingApplication, cancelled: () -> Bool) throws { commands.append(command); operations.append("command") }
        func requestPermission(for app: NowPlayingApplication, cancelled: () -> Bool) throws {}
        func volume(in app: NowPlayingApplication, cancelled: () -> Bool) throws -> Double? { currentVolume }
        func setVolume(_ value: Double, in app: NowPlayingApplication, cancelled: () -> Bool) throws { volumes.append(value); currentVolume = value; operations.append("volume") }
        func subscribeToChanges(_ action: @escaping () -> Void) -> (() -> Void) {
            subscriptions += 1
            return { [weak self] in self?.cancellations += 1 }
        }
    }
}
