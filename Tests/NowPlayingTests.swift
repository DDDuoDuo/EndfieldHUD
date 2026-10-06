import AppKit
import Carbon
import CoreAudio
import QuartzCore

enum NowPlayingTests {
    static func run() -> Int {
        _ = NSApplication.shared
        CATransaction.begin(); CATransaction.setDisableActions(true)
        defer { CATransaction.commit() }
        var count = 0
        func check(_ value: Bool, _ message: String, file: StaticString = #file, line: UInt = #line) {
            count += 1; if !value { fatalError(message, file: file, line: line) }
        }
        let language = L10n.language; L10n.language = .english; defer { L10n.language = language }
        let music = NowPlayingApplication(source: .music, pid: 101, bundleURL: URL(fileURLWithPath: "/fixture/Music.app"))
        let spotify = NowPlayingApplication(source: .spotify, pid: 102, bundleURL: URL(fileURLWithPath: "/fixture/Spotify.app"))
        let track = NowPlayingTrack(title: "A \"track\"\n你好", artist: "Artist", album: "Album", duration: 200,
                                    position: 25, isPlaying: true, sampledAt: 10)
        check(track.elapsed(at: 15) == 30 && track.elapsed(at: 5) == 25 && track.elapsed(at: 1_000) == 200,
              "Monotonic local progress clamps at the track end and never runs backwards")
        let paused = NowPlayingTrack(title: "Paused", artist: "", album: "", duration: 200, position: 50, isPlaying: false, sampledAt: 10)
        check(paused.elapsed(at: 100) == 50, "Paused progress remains fixed without polling")
        let malformed = NowPlayingTrack(title: String(repeating: "字", count: 5_000), artist: "", album: "", duration: .infinity,
                                        position: .nan, isPlaying: true, sampledAt: .nan)
        check(malformed.title.count == 512 && malformed.duration == nil && malformed.position == nil,
              "Malformed numeric telemetry and excessive metadata are bounded")
        check(NowPlayingCanvas.time(nil) == "—:—" && NowPlayingCanvas.time(61) == "1:01", "Progress formatting represents unavailable values honestly")

        let backend = Backend(); backend.tracks[.music] = track; backend.tracks[.spotify] = paused
        let harness = Harness(); var apps = [music, spotify]
        let controller = NowPlayingController(backend: backend, applications: { apps }, subscribe: harness.subscribe,
                                             work: harness.schedule, deliver: { $0() })
        var events: [NowPlayingEvent] = []; controller.onEvent = { events.append($0) }
        controller.refresh(); controller.connect(); controller.perform(.playPause)
        check(harness.work.isEmpty && backend.reads.isEmpty && backend.permissions.isEmpty, "Inactive methods cannot query or control real players")
        controller.activate(); controller.activate(); controller.connect()
        check(!controller.isRequestingPermission, "Busy metadata reads cannot create a permission-presentation lifetime")
        check(harness.subscriptions == 1 && harness.work.count == 1 && backend.reads.isEmpty,
              "Activation registers once and enqueues player work without blocking navigation")
        for _ in 0..<1_000 { harness.callback?(.spotify) }
        check(harness.work.count == 1, "Notification bursts retain only one worker and one pending refresh")
        harness.drain()
        check(backend.reads.count == 4 && controller.snapshot.application == music && controller.snapshot.track == track,
              "A coalesced follow-up discovers the actually playing supported source")
        check(backend.permissions.isEmpty && events.isEmpty, "Observation cannot prompt or emit playback-action logs")
        let reads = backend.reads.count
        for i in 0..<10_000 { _ = controller.snapshot.track?.elapsed(at: Double(i)) }
        check(reads == backend.reads.count && harness.work.isEmpty, "Visual interpolation never queries the player")
        controller.perform(.seek(.nan)); check(harness.work.isEmpty, "Nonfinite seeks do not reach the backend")
        controller.perform(.seek(500)); controller.perform(.next)
        check(harness.work.count == 1, "Repeated controls cannot create an unbounded command queue")
        harness.drain()
        check(backend.commands.map { $0.1 } == [.seek(200), .next] && events.count == 2,
              "Seek clamps and the latest queued user action runs once after it completes")
        controller.select(.spotify); harness.drain()
        check(controller.snapshot.application == spotify && controller.snapshot.track == paused, "Explicit player selection remains stable")
        backend.failures[.spotify] = .permissionDenied
        controller.refresh(); harness.drain(); controller.perform(.playPause)
        check(controller.snapshot.failure == .permissionDenied && harness.work.isEmpty,
              "Denied automation disables commands without another permission prompt")
        controller.connect()
        check(controller.isRequestingPermission, "An explicit Connect keeps the HUD open for the actual permission-request lifetime")
        harness.drain()
        check(!controller.isRequestingPermission, "Permission completion clears the focus-loss exemption")
        check(backend.permissions == [.spotify], "Only explicit Connect requests player authorization")
        backend.failures.removeValue(forKey: .spotify)
        controller.refresh(); harness.drain(); backend.commandFailure = .timedOut
        let eventCount = events.count; controller.perform(.next); harness.drain()
        check(events.count == eventCount && controller.snapshot.failure == .timedOut,
              "Failed player commands neither claim success nor log playback")
        backend.commandFailure = nil
        controller.refresh(); harness.drain()
        controller.perform(.previous); controller.deactivate(); harness.drain()
        check(events.count == eventCount && controller.snapshot.track == nil && harness.cancellations == 1,
              "Closing cancels queued commands, drops stale replies and releases metadata")
        let commands = backend.commands.count
        harness.callback?(.music); harness.drain()
        check(backend.commands.count == commands && harness.work.isEmpty, "Late notification delivery cannot revive a hidden service")
        apps = []; controller.activate(); harness.drain()
        check(controller.snapshot.application == nil && controller.snapshot.failure == .unavailable && backend.permissions.count == 1,
              "No running player produces an honest unavailable state without launching or asking")
        controller.deactivate()

        let permissionHarness = Harness()
        let permissionService = NowPlayingController(backend: backend, applications: { [music] }, subscribe: { _ in {} },
                                                     work: permissionHarness.schedule, deliver: { $0() })
        permissionService.activate(); permissionHarness.drain(); permissionService.connect()
        check(permissionService.isRequestingPermission, "Deferred permission work exposes its presentation flag")
        permissionService.deactivate(); permissionHarness.drain()
        check(!permissionService.isRequestingPermission && permissionService.snapshot.track == nil,
              "Deactivation clears the permission flag and stale completion cannot restore it")

        let racing = Harness(); let racingBackend = Backend(); racingBackend.tracks[.music] = track; racingBackend.tracks[.spotify] = paused
        let racingController = NowPlayingController(backend: racingBackend, applications: { [music, spotify] }, subscribe: racing.subscribe,
                                                    work: racing.schedule, deliver: { $0() })
        racingController.activate(); racingController.select(.spotify); racing.drain()
        check(racingController.snapshot.application == spotify && racingBackend.reads == [.spotify],
              "Changing player cancels the old queued read and applies only the latest selection")
        racingController.deactivate()
        var disposable: NowPlayingController? = NowPlayingController(backend: Backend(), applications: { [music] }, subscribe: { _ in {} },
                                                                    work: { _ in }, deliver: { $0() })
        weak var weakService = disposable; disposable?.activate(); disposable = nil
        check(weakService == nil, "Queued work and observer callbacks cannot retain the controller")

        let xml = """
        <dictionary><suite name="Player" code="test">
        <command name="playpause" code="testPlPs"/><command name="previous track" code="testPrev"/><command name="next track" code="testNext"/>
        <class name="application" code="capp"><property name="player state" code="pPlS"/><property name="player position" code="pPos"/><property name="current track" code="pTrk"/></class>
        <class name="track" code="cTrk"><property name="name" code="pnam"/><property name="artist" code="pArt"/><property name="album" code="pAlb"/><property name="duration" code="pDur"/><property name="persistent ID" code="pPIS"/><property name="artwork url" code="aURL"/></class>
        <class name="artwork" code="cArt"><property name="raw data" code="pRaw"/></class>
        <enumeration name="state" code="ePlS"><enumerator name="playing" code="kPSP"/><enumerator name="paused" code="kPSp"/><enumerator name="stopped" code="kPSS"/></enumeration>
        </suite></dictionary>
        """
        let dictionary = NowPlayingScriptingDictionary(data: Data(xml.utf8))
        check(dictionary?.commands["playpause"]?.0 == NowPlayingAppleEvents.code("test")
              && dictionary?.properties["player position"] == NowPlayingAppleEvents.code("pPos"),
              "Actual dictionary terminology determines public command and property codes")
        check(dictionary?.classes["artwork"] == NowPlayingAppleEvents.code("cArt")
              && dictionary?.properties["raw data"] == NowPlayingAppleEvents.code("pRaw")
              && dictionary?.properties["artwork url"] == NowPlayingAppleEvents.code("aURL"),
              "Optional artwork capabilities come from the player's dictionary without becoming required playback fields")
        let artworkSpec = NowPlayingAppleEvents.firstElement(NowPlayingAppleEvents.code("cArt"), container: specForTrack())
        check(artworkSpec.descriptorType == typeObjectSpecifier
              && artworkSpec.forKeyword(AEKeyword(keyAEKeyForm))?.enumCodeValue == OSType(formAbsolutePosition)
              && artworkSpec.forKeyword(AEKeyword(keyAEKeyData))?.int32Value == 1,
              "Music artwork uses the public first-artwork object in the current track, not an unverified event")
        check(NowPlayingScriptingDictionary(data: Data("<dictionary/>".utf8)) == nil,
              "Missing scripting capabilities cannot become fabricated enabled controls")
        check(NowPlayingScriptingDictionary(data: Data(repeating: 65, count: 2_097_153)) == nil, "Dictionary input is bounded")
        let spec = NowPlayingAppleEvents.property(NowPlayingAppleEvents.code("pPos"))
        check(spec.descriptorType == typeObjectSpecifier
              && spec.forKeyword(AEKeyword(keyAEKeyData))?.typeCodeValue == NowPlayingAppleEvents.code("pPos"),
              "Public property addressing encodes a real Apple Event object specifier")
        check(NowPlayingAppleEvents.number(NSAppleEventDescriptor(double: 17.5)) == 17.5,
              "Playback position retains fractional seconds")
        check(NowPlayingAppleEvents.nonPromptingSendOptions.rawValue & UInt(kAEDoNotPromptForUserConsent) != 0
              && NowPlayingAppleEvents.nonPromptingSendOptions.contains(.neverInteract)
              && NowPlayingAppleEvents.nonPromptingSendOptions.contains(.dontRecord),
              "Ordinary Apple Events explicitly prohibit consent prompts even if the preflight becomes stale")

        let canvasBackend = Backend(); canvasBackend.tracks[.music] = track
        let service = NowPlayingController(backend: canvasBackend, applications: { [music] }, subscribe: { _ in {} }, work: { $0() }, deliver: { $0() })
        var audioState = AudioDeviceSnapshot(); audioState.outputs = [AudioDeviceInfo(id: 3, name: "Fixture", outputChannels: 2, transportType: kAudioDeviceTransportTypeBuiltIn)]
        audioState.defaultOutputID = 3; audioState.applicationActivitySupported = true
        audioState.availableApplications = [AudioApplicationInfo(id: 11, pid: music.pid, bundleIdentifier: music.source.bundleIdentifier,
            name: "Music", outputDeviceIDs: [3], applicationURL: music.bundleURL)]
        let audio = AudioDeviceController(snapshot: audioState), perApp = PerAppAudioController.fixture()
        let canvas = NowPlayingCanvas(controller: service, audio: audio, perAppAudio: perApp, now: { 10 }, reduceMotion: { false })
        let rendered = canvas.makeContent(for: .nowPlaying, style: HUDModuleContentStyle(dark: true, accent: .systemYellow, contentsScale: 2))
        check(canvasBackend.reads.isEmpty && !audio.isRunning && perApp.sessions.isEmpty,
              "Building artwork never starts playback observation or audio routes")
        canvas.activate()
        check(audio.isRunning && canvas.accessibleSliders.allSatisfy(\.enabled) && perApp.sessions.isEmpty,
              "Visible controls reuse shared audio observation and leave direct playback untouched")
        let seek = canvas.accessibleSliders[0]
        _ = canvas.mouseDown(at: CGPoint(x: seek.rect.minX, y: seek.rect.midY))
        for step in 0...100 { canvas.mouseDragged(to: CGPoint(x: seek.rect.minX + CGFloat(step) * 3, y: seek.rect.midY)) }
        check(canvasBackend.commands.isEmpty && canvas.isDragging, "Dragging progress only previews; it sends no per-frame Apple Events")
        canvas.mouseUp()
        check(canvasBackend.commands.count == 1 && !canvas.isDragging, "Progress commits one player seek at release")
        _ = canvas.mouseDown(at: CGPoint(x: seek.rect.midX, y: seek.rect.midY))
        canvasBackend.tracks[.music] = NowPlayingTrack(title: track.title, artist: track.artist, album: track.album,
            duration: track.duration, position: 37, isPlaying: false, sampledAt: 22)
        service.refresh()
        check(canvas.isDragging, "A same-track refresh of position, sample time and pause state preserves the seek gesture")
        canvas.mouseUp()
        check(canvasBackend.commands.count == 2 && canvasBackend.commands.last?.1 == .seek(100),
              "A same-track notification cannot silently drop the user's committed seek")
        _ = canvas.mouseDown(at: CGPoint(x: seek.rect.midX, y: seek.rect.midY))
        canvasBackend.tracks[.music] = NowPlayingTrack(title: "Different track", artist: track.artist, album: track.album,
            duration: track.duration, position: 0, isPlaying: true, sampledAt: 23)
        service.refresh(); canvas.mouseUp()
        check(!canvas.isDragging && canvasBackend.commands.count == 2,
              "An actual track change cancels the old gesture before it can seek the new track")
        canvasBackend.tracks[.music] = track; service.refresh()
        func descendants(_ root: CALayer) -> [CALayer] { [root] + (root.sublayers ?? []).flatMap(descendants) }
        let fill = descendants(rendered).first { $0.name == "nowPlaying.progress.fill" }!
        let handle = descendants(rendered).first { $0.name == "nowPlaying.progress.handle" }!
        canvas.tickForVerification()
        let fillMotion = fill.animation(forKey: "nowPlaying.progress") as? CABasicAnimation
        let handleMotion = handle.animation(forKey: "nowPlaying.progress") as? CABasicAnimation
        check(fillMotion != nil && handleMotion != nil
              && abs(fill.bounds.width - (fillMotion?.toValue as? NSNumber)!.doubleValue) < 0.000_001
              && abs(handle.position.x - (handleMotion?.toValue as? NSNumber)!.doubleValue) < 0.000_001,
              "Progress interpolation stores its destination in the model so completed tracks cannot snap backwards")
        _ = canvas.mouseDown(at: CGPoint(x: seek.rect.midX, y: seek.rect.midY))
        check(fill.animation(forKey: "nowPlaying.progress") == nil && handle.animation(forKey: "nowPlaying.progress") == nil
              && abs(fill.bounds.width - seek.rect.width / 2) < 0.000_001 && handle.position.x == seek.rect.midX,
              "Seek preview cancels old interpolation and immediately places both retained progress layers")
        canvas.mouseUp(); canvas.tickForVerification()
        service.refresh()
        check(fill.animation(forKey: "nowPlaying.progress") == nil && handle.animation(forKey: "nowPlaying.progress") == nil,
              "A metadata replacement removes stale progress tracks before the next visual tick")
        let retainedIDs = descendants(rendered).map(ObjectIdentifier.init)
        canvas.perform(actionID: "playPause"); service.refresh()
        check(descendants(rendered).map(ObjectIdentifier.init) == retainedIDs,
              "Pause and metadata updates retain every music panel layer instead of flashing a rebuilt tree")
        check(canvas.panelFrameForVerification == CGRect(x: 55, y: 0, width: 330, height: 330)
              && rendered.bounds.size == CGSize(width: 440, height: 440)
              && canvas.accessibleSliders[0].rect.midX == rendered.bounds.midX,
              "Enlarged square artwork and the seek row remain centered in the widened music plane")
        let panel = descendants(rendered).first { $0.name == "nowPlaying.panel" }!
        check(panel.backgroundColor == nil && panel.borderWidth == 0,
              "The music surface has no surrounding background or edge")
        let transportRects = canvas.accessibleActions.filter { ["previous", "playPause", "next"].contains($0.id) }.map(\.rect)
        let extraRects = canvas.accessibleActions.filter { ["volume", "lyrics"].contains($0.id) }.map(\.rect)
        check(transportRects.map(\.maxX).max()! + 220 <= extraRects.map(\.minX).min()!
              && canvas.accessibleActions.allSatisfy { rendered.bounds.contains($0.rect) },
              "Bottom-left transport and bottom-right extras leave a clear battery gap of at least 220 points")
        let batteryFootprint = CGRect(x: rendered.bounds.midX - 108.24, y: 413.5, width: 216.48, height: 31)
        check(canvas.accessibleActions.allSatisfy { !$0.rect.intersects(batteryFootprint) },
              "The bottom controls clear the centered expanded battery footprint at the same vertical level")
        check(!canvas.accessibleActions.contains(where: { $0.id == "music" || $0.id == "spotify" || $0.id == "connect" }),
              "Player discovery is automatic without manual source selection controls")
        check(!canvas.accessibleSliders.contains(where: { $0.id == "appVolume" }), "Volume remains inside its secondary menu")
        canvas.perform(actionID: "volume")
        check(canvas.capturesPointer && canvas.accessibleSliders.last!.rect.height > canvas.accessibleSliders.last!.rect.width,
              "Volume opens a projected vertical slider menu")
        check(canvas.setSlider(id: "appVolume", value: 0.45) && perApp.sessions.count == 1 && perApp.sessions[0].pid == music.pid,
              "App volume creates only the existing explicit route for the matching playing application")
        let routed = perApp.sessions[0].processID
        check(canvas.setSlider(id: "appVolume", value: 1) && perApp.sessions[0].processID == routed,
              "Returning to 100 percent reuses the same route instead of recreating capture")
        canvas.dismissPopover()
        check(!canvas.capturesPointer && canvas.accessibleSliders.count == 1, "Closing volume removes its input target")
        let host = NSView(frame: CGRect(x: 0, y: 0, width: 800, height: 668))
        let interaction = HUDNowPlayingInteraction(canvas: canvas, host: host)
        interaction.project = { CGRect(x: $0.minX * 2, y: $0.minY * 2, width: $0.width * 2, height: $0.height * 2) }
        interaction.setActive(true)
        let seekControl = host.subviews.compactMap { $0 as? NSSlider }.first { $0.accessibilityLabel() == "Playback position" }
        check(seekControl?.frame.width == seek.rect.width * 2 && seekControl?.isContinuous == false,
              "Native accessibility uses projected geometry and noncontinuous seek commits")
        let controlCount = host.subviews.count
        for _ in 0..<100 { interaction.layoutAccessibility() }
        check(host.subviews.count == controlCount, "Accessibility updates retain a bounded set of native controls")
        interaction.deactivate()
        func animationCount(_ layer: CALayer) -> Int { (layer.animationKeys()?.count ?? 0) + (layer.sublayers ?? []).reduce(0) { $0 + animationCount($1) } }
        check(!audio.isRunning && animationCount(rendered) == 0 && host.subviews.allSatisfy(\.isHidden),
              "Leaving removes progress animations and hides all native controls")
        check(perApp.sessions.count == 1, "Leaving Now Playing preserves the explicitly enabled existing audio route")
        perApp.stopAll()

        // Defer the fake reads so these assertions cover the busy interval,
        // not merely the already-settled synchronous fixture above.
        let feedbackBackend = Backend(); feedbackBackend.tracks[.music] = paused; feedbackBackend.nativeVolume = 0.42
        let feedbackWork = Harness()
        let feedbackService = NowPlayingController(backend: feedbackBackend, applications: { [music] }, subscribe: { _ in {} },
                                                   work: feedbackWork.schedule, deliver: { $0() })
        let feedbackAudio = AudioDeviceController(snapshot: AudioDeviceSnapshot())
        let feedbackRoutes = PerAppAudioController.fixture()
        let feedbackCanvas = NowPlayingCanvas(controller: feedbackService, audio: feedbackAudio, perAppAudio: feedbackRoutes,
                                             now: { 10 }, reduceMotion: { false })
        let feedbackLayer = feedbackCanvas.makeContent(for: .nowPlaying,
            style: HUDModuleContentStyle(dark: true, accent: .systemYellow, contentsScale: 2))
        feedbackCanvas.activate()
        check(feedbackService.snapshot.busy && !feedbackCanvas.accessibleActions.contains { $0.id == "refresh" },
              "Automatic activation does not expose a manual refresh button")
        feedbackWork.drain()
        feedbackService.refresh()
        check(feedbackService.snapshot.busy && !descendants(feedbackLayer).contains { $0.name == "nowPlaying.control.refresh" },
              "Ordinary notification refreshes never create hidden refresh artwork")
        feedbackWork.drain()
        check(!descendants(feedbackLayer).compactMap { ($0 as? CATextLayer)?.string as? String }.contains("Now Playing"),
              "The removed music heading does not survive as an empty or stale text layer")
        feedbackCanvas.perform(actionID: "refresh")
        check(feedbackWork.work.isEmpty, "A removed refresh action cannot create invisible work")
        feedbackWork.drain()
        feedbackBackend.failures[.music] = .timedOut
        feedbackService.refresh(); feedbackWork.drain()
        let transport = feedbackCanvas.accessibleActions.filter { ["previous", "playPause", "next"].contains($0.id) }
        check(feedbackService.snapshot.track == paused && feedbackService.snapshot.failure == .timedOut
              && transport.count == 3 && transport.allSatisfy { !$0.enabled },
              "A retained track after a failed read cannot expose enabled transport actions that silently do nothing")
        feedbackBackend.failures.removeValue(forKey: .music)
        feedbackService.refresh(); feedbackWork.drain()
        check(feedbackCanvas.accessibleActions.filter { ["previous", "playPause", "next"].contains($0.id) }.allSatisfy(\.enabled),
              "A successful refresh restores transport controls after a transient failure")
        let feedbackHost = NSView(frame: CGRect(x: 0, y: 0, width: 440, height: 440))
        let feedbackInteraction = HUDNowPlayingInteraction(canvas: feedbackCanvas, host: feedbackHost)
        feedbackInteraction.setActive(true)
        feedbackCanvas.perform(actionID: "volume")
        let nativeVolume = feedbackHost.subviews.compactMap { $0 as? NSSlider }
            .first { $0.accessibilityLabel() == "Playing app volume" }
        check(nativeVolume?.isEnabled == true && nativeVolume?.doubleValue == 0.42
              && nativeVolume?.accessibilityHelp() == "Playing app volume" && feedbackRoutes.sessions.isEmpty,
              "A paused player's native volume exposes truthful accessibility help without an audio capture process")
        feedbackInteraction.deactivate()

        let lyricBackend = Backend(), lyricWork = Harness()
        var visualNow: TimeInterval = 100, reduced = false
        func lyricTrack(_ title: String = "First song", position: Double = 0, playing: Bool = true) -> NowPlayingTrack {
            NowPlayingTrack(title: title, artist: "Fixture artist", album: "Fixture album", duration: 200,
                position: position, isPlaying: playing, sampledAt: visualNow,
                timedLyrics: "[00:00.00]First line\n[00:20.00]Second line\n[00:50.00]Third line\n[01:40.00]Last line")
        }
        lyricBackend.tracks[.music] = lyricTrack()
        let lyricService = NowPlayingController(backend: lyricBackend, applications: { [music] }, subscribe: { _ in {} },
            work: lyricWork.schedule, deliver: { $0() })
        let lyricCanvas = NowPlayingCanvas(controller: lyricService, audio: AudioDeviceController(snapshot: AudioDeviceSnapshot()),
            perAppAudio: PerAppAudioController.fixture(), now: { visualNow }, reduceMotion: { reduced })
        let lyricStyle = HUDModuleContentStyle(dark: true, accent: .systemYellow, contentsScale: 2)
        let lyricRoot = lyricCanvas.makeContent(for: .nowPlaying, style: lyricStyle)
        lyricCanvas.activate(); lyricWork.drain()
        let lyricViewport = descendants(lyricRoot).first { $0.name == "nowPlaying.lyrics" }!
        let lyricRows = lyricViewport.sublayers!
        check(lyricRows.count == 3 && lyricViewport.masksToBounds,
              "Lyrics keep exactly three clipped retained rows")
        let shade = descendants(lyricRoot).first { $0.name == "nowPlaying.metadataShade" }!
        check(shade.frame.height == 80 && shade.frame.maxY == 330 && shade.frame.minY == 250,
              "Metadata contrast is limited to the shallow bottom of the square cover")
        descendants(lyricRoot).forEach { $0.removeAllAnimations() }
        visualNow = 121; lyricCanvas.tickForVerification()
        check(lyricRows.allSatisfy { ($0.animation(forKey: "nowPlaying.lyrics.change") as? CAAnimationGroup)?.duration == 0.20 },
              "Advancing lyrics uses short finite transitions on the existing rows")
        lyricRows.forEach { $0.removeAllAnimations() }; visualNow = 122; lyricCanvas.tickForVerification()
        check(lyricRows.allSatisfy { $0.animationKeys() == nil },
              "Progress within the same lyric never restarts its transition")
        lyricBackend.tracks[.music] = lyricTrack(position: 1, playing: false)
        lyricService.refresh(); lyricWork.drain()
        check(lyricRows.allSatisfy {
            let group = $0.animation(forKey: "nowPlaying.lyrics.change") as? CAAnimationGroup
            return (group?.animations?.first as? CABasicAnimation)?.fromValue as? Int == -7
        }, "A backward seek transitions lyric rows in the opposite direction")
        lyricBackend.tracks[.music] = lyricTrack("Second song", position: 1, playing: false)
        lyricService.refresh(); lyricWork.drain()
        let newTitle = descendants(lyricRoot).compactMap { $0 as? CATextLayer }.first { $0.string as? String == "Second song" }!
        check((newTitle.animation(forKey: kCATransition) as? CATransition)?.duration == 0.20,
              "Changing song metadata crossfades its retained title")
        descendants(lyricRoot).forEach { $0.removeAllAnimations() }
        lyricService.refresh(); lyricWork.drain()
        check(newTitle.animation(forKey: kCATransition) == nil,
              "An unchanged metadata sample never replays the song transition")
        let lowerRail = lyricCanvas.accessibleSliders[0].rect
        let bottomControls = lyricCanvas.accessibleActions.map(\.rect)
        lyricCanvas.perform(actionID: "volume")
        let volumeSlider = lyricCanvas.accessibleSliders.last!.rect
        lyricCanvas.perform(actionID: "lyrics")
        check(lyricCanvas.accessibleSliders[0].rect == lowerRail
              && lyricCanvas.accessibleActions.map(\.rect) == bottomControls
              && lyricCanvas.accessibleSliders.last!.rect == volumeSlider
              && lyricCanvas.panelFrameForVerification.size == CGSize(width: 330, height: 330),
              "Hiding lyrics keeps the seek row, bottom controls, volume popup and enlarged square cover fixed")
        check(lyricViewport.opacity == 0 && lyricViewport.animation(forKey: "nowPlaying.lyrics.visibility") != nil
              && !descendants(lyricRoot).contains { $0.animation(forKey: "nowPlaying.lyrics.move") != nil || $0.animation(forKey: "nowPlaying.control.move") != nil },
              "Only lyric opacity animates when hiding the reserved rows; no transport position animation is added")
        lyricCanvas.perform(actionID: "lyrics")
        check(lyricViewport.opacity == 1 && lyricCanvas.accessibleActions.map(\.rect) == bottomControls
              && lyricCanvas.accessibleSliders[0].rect == lowerRail,
              "Showing lyrics restores the same reserved rows without shifting controls")
        lyricCanvas.dismissPopover()
        reduced = true; _ = lyricCanvas.makeContent(for: .nowPlaying, style: lyricStyle)
        check(animationCount(lyricRoot) == 0, "Reduce Motion immediately removes every music finite transition")
        lyricCanvas.deactivate()
        check(!lyricCanvas.progressClockActiveForVerification && animationCount(lyricRoot) == 0,
              "Hidden music retains no lyric, song or progress animation")

        // Exercise the real canvas scheduler at fractional cues with an injected
        // monotonic clock. No sleeping, player commands or network requests.
        let cueBackend = Backend(), cueWork = Harness(), displayClock = DisplayClock()
        var cueNow: TimeInterval = 500, cueReduced = false
        let cueText = "[00:00]Start\n[00:00.25]Quarter\n[00:00.60]Next\n[00:02]Last"
        func cueTrack(position: Double, playing: Bool = true, lyrics: String = cueText, duration: Double = 10) -> NowPlayingTrack {
            NowPlayingTrack(title: "Fractional cues", artist: "Fixture", album: "", duration: duration,
                position: position, isPlaying: playing, sampledAt: cueNow, timedLyrics: lyrics)
        }
        cueBackend.tracks[.music] = cueTrack(position: 0.1)
        let cueService = NowPlayingController(backend: cueBackend, applications: { [music] }, subscribe: { _ in {} },
            work: cueWork.schedule, deliver: { $0() })
        let cueCanvas = NowPlayingCanvas(controller: cueService, audio: AudioDeviceController(snapshot: AudioDeviceSnapshot()),
            perAppAudio: PerAppAudioController.fixture(), now: { cueNow }, reduceMotion: { cueReduced },
            scheduleDisplayUpdate: displayClock.schedule)
        let cueRoot = cueCanvas.makeContent(for: .nowPlaying, style: lyricStyle)
        let cueRows = descendants(cueRoot).first { $0.name == "nowPlaying.lyrics" }!.sublayers!.compactMap { $0 as? CATextLayer }
        cueCanvas.activate(); cueWork.drain()
        check(displayClock.pending.count == 1 && abs(displayClock.pending[0].delay - 0.15) < 0.0001,
              "One shared display deadline targets the fractional lyric cue instead of waiting for the next second")
        check(cueRows[1].string as? String == "Start", "A scheduled cue never becomes current before its timestamp")
        let cueReads = cueBackend.reads.count
        cueNow += 0.151; displayClock.fire()
        check(cueRows[1].string as? String == "Quarter" && cueBackend.reads.count == cueReads,
              "The first subsecond deadline advances lyrics immediately using local time without querying the player")
        check(displayClock.pending.count == 1 && abs(displayClock.pending[0].delay - 0.349) < 0.0001,
              "The next lyric and progress update share exactly one cancellable deadline")
        let staleBeforePause = displayClock.pending[0].action
        cueBackend.tracks[.music] = cueTrack(position: 0.251, playing: false)
        cueService.refresh(); cueWork.drain()
        check(displayClock.pending.isEmpty && !cueCanvas.progressClockActiveForVerification,
              "Pausing cancels the pending fractional display clock")
        cueNow += 10; staleBeforePause()
        check(cueRows[1].string as? String == "Quarter" && displayClock.pending.isEmpty,
              "A canceled deadline cannot advance paused lyrics or restart the display clock")
        cueBackend.tracks[.music] = cueTrack(position: 0.1)
        cueService.refresh(); cueWork.drain()
        check(displayClock.pending.count == 1 && abs(displayClock.pending[0].delay - 0.15) < 0.0001,
              "Resuming or seeking rebuilds the cue deadline from the current playback anchor")
        let staleHidden = displayClock.pending[0].action
        cueCanvas.perform(actionID: "lyrics")
        check(displayClock.pending.count == 1 && abs(displayClock.pending[0].delay - 0.9) < 0.0001,
              "Hidden lyric rows add no cue wakeups; only the existing whole-second progress update remains")
        staleHidden()
        check(displayClock.pending.count == 1, "A canceled lyric wakeup cannot duplicate the remaining progress deadline")
        cueCanvas.perform(actionID: "lyrics")
        check(displayClock.pending.count == 1 && abs(displayClock.pending[0].delay - 0.15) < 0.0001,
              "Showing lyrics restores their precise deadline without moving controls or starting another service")
        cueReduced = true; cueNow += 0.151; displayClock.fire()
        check(cueRows[1].string as? String == "Quarter" && cueRows.allSatisfy { $0.animationKeys() == nil },
              "Reduce Motion keeps accurate cue timing while removing transition animation")
        cueBackend.tracks[.music] = cueTrack(position: 0.095,
            lyrics: "[00:00]Before\n[00:00.10]Dense one\n[00:00.11]Dense two\n[00:00.12]Dense three")
        cueService.refresh(); cueWork.drain()
        check(displayClock.pending.count == 1 && displayClock.pending[0].delay >= 1.0 / 30.0,
              "Dense millisecond cue clusters cannot create an unbounded deadline loop")
        cueNow += 0.034; displayClock.fire()
        check(cueRows[1].string as? String == "Dense three" && displayClock.pending.count == 1,
              "A dense cluster coalesces directly to the current line without replaying stale intermediate lyrics")
        cueBackend.tracks[.music] = cueTrack(position: 0.9, duration: 1)
        cueService.refresh(); cueWork.drain()
        let beforeEnd = cueBackend.reads.count
        cueNow += 0.101; displayClock.fire(); cueWork.drain()
        check(cueBackend.reads.count == beforeEnd + 1 && displayClock.pending.isEmpty,
              "Reaching the end requests metadata once and leaves no stale local progress clock running")
        cueBackend.tracks[.music] = cueTrack(position: 0.1)
        cueService.refresh(); cueWork.drain()
        let afterClose = displayClock.pending[0].action
        cueCanvas.deactivate(); let hiddenReads = cueBackend.reads.count
        afterClose()
        check(displayClock.pending.isEmpty && !cueCanvas.progressClockActiveForVerification
              && cueBackend.reads.count == hiddenReads && animationCount(cueRoot) == 0,
              "Closing removes the only display deadline, rejects stale callbacks and stops all lyric animations")
        return count
    }

    private static func specForTrack() -> NSAppleEventDescriptor { NowPlayingAppleEvents.property(NowPlayingAppleEvents.code("pTrk")) }

    private final class Backend: NowPlayingBackend {
        var tracks: [NowPlayingSource: NowPlayingTrack] = [:]
        var failures: [NowPlayingSource: NowPlayingFailure] = [:]
        var commandFailure: NowPlayingFailure?
        var nativeVolume: Double?
        var reads: [NowPlayingSource] = []
        var commands: [(NowPlayingSource, NowPlayingCommand)] = []
        var permissions: [NowPlayingSource] = []
        func read(_ app: NowPlayingApplication, cancelled: () -> Bool) throws -> NowPlayingTrack? {
            if cancelled() { throw NowPlayingFailure.cancelled }; reads.append(app.source)
            if let error = failures[app.source] { throw error }; return tracks[app.source]
        }
        func perform(_ command: NowPlayingCommand, in app: NowPlayingApplication, cancelled: () -> Bool) throws {
            if cancelled() { throw NowPlayingFailure.cancelled }
            if let commandFailure { throw commandFailure }; commands.append((app.source, command))
        }
        func requestPermission(for app: NowPlayingApplication, cancelled: () -> Bool) throws {
            if cancelled() { throw NowPlayingFailure.cancelled }; permissions.append(app.source)
            if let error = failures[app.source] { throw error }
        }
        func volume(in app: NowPlayingApplication, cancelled: () -> Bool) throws -> Double? {
            if cancelled() { throw NowPlayingFailure.cancelled }; return nativeVolume
        }
    }
    private final class Harness {
        var work: [() -> Void] = []
        var subscriptions = 0
        var cancellations = 0
        var callback: ((NowPlayingSource?) -> Void)?
        func subscribe(_ action: @escaping (NowPlayingSource?) -> Void) -> () -> Void {
            subscriptions += 1; callback = action; return { [weak self] in self?.cancellations += 1 }
        }
        func schedule(_ action: @escaping () -> Void) { work.append(action) }
        func drain() {
            var count = 0
            while !work.isEmpty { count += 1; precondition(count < 20, "Unbounded Now Playing worker loop"); work.removeFirst()() }
        }
    }
    private final class DisplayClock {
        final class Entry {
            let delay: TimeInterval
            let action: () -> Void
            var canceled = false
            init(_ delay: TimeInterval, _ action: @escaping () -> Void) { self.delay = delay; self.action = action }
        }
        private var entries: [Entry] = []
        var pending: [Entry] { entries.filter { !$0.canceled } }
        func schedule(_ delay: TimeInterval, _ action: @escaping () -> Void) -> (() -> Void) {
            entries.removeAll { $0.canceled }
            let entry = Entry(delay, action); entries.append(entry)
            return { entry.canceled = true }
        }
        func fire() { let entry = pending[0]; entry.canceled = true; entry.action() }
    }
}
