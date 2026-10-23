import AppKit
import QuartzCore

/// Now Playing (spec §3.12): the v1.2.0 player, larger. There is no module
/// header: the 480 px cover reaches above the box with the title shade on it,
/// three centred lyric lines and the timeline sit under it, and one button row
/// runs level with the shell's battery chip (transport on the left, volume and
/// lyrics on the right). Metadata, transport and lyric changes update the page
/// in place; another track re-enters from the right (from the left after
/// Previous). Player observation follows the host's presentation, which starts
/// while the HUD reveals; one visible-only deadline advances the readouts at
/// whole seconds and lyric cues, and the bar interpolates on the compositor in
/// between without sampling a player.
final class NowPlayingCanvas: HUDDesignCanvas {
    typealias DisplaySchedule = (TimeInterval, @escaping () -> Void) -> (() -> Void)
    /// Successful transport commands (the Event Log records them).
    var onEvent: ((NowPlayingEvent) -> Void)?
    private(set) var isPresented = false
    private(set) var presentationStartCountForVerification = 0
    private(set) var isVolumeOpen = false
    private(set) var showsLyrics = true
    var isRequestingPermission: Bool { controller.isRequestingPermission }
    var progressClockActiveForVerification: Bool { cancelClock != nil }
    var albumCoverAvailableForVerification: Bool { controller.artworkImage != nil }
    var panelFrameForVerification: CGRect { Layout.cover }
    /// The permission prompt keeps the HUD up while it has focus.
    override var isPresentingPanel: Bool { isPresented && isRequestingPermission }
    override var hitBounds: CGRect { Layout.hitBounds }
    override var capturesPointer: Bool { super.capturesPointer || isVolumeOpen }

    private enum Layout {
        /// Centred and 480 px (the demo's 420 enlarged): it reaches 84 px above
        /// the box, still clear of the shell's charge readout over it.
        static let cover = CGRect(x: 55, y: -84, width: 480, height: 480)
        /// Lyrics and the timeline span the cover's width under it.
        static let lyrics = CGRect(x: 55, y: 400, width: 480, height: 60)
        static let timeline = CGRect(x: 55, y: 464, width: 480, height: 34)
        /// Previous, play and next with a 10 px gap, top 516. The spec's left
        /// −6 moves 30 px out so Next clears the shell's charge capsule
        /// (≈325 px wide, centred under the box) by 6 px, as in v1.2.0.
        static let transport = CGRect(x: -36, y: 516, width: 162, height: 54)
        /// Volume and lyrics, 8 px apart, right −6 and top 527.
        static let extras = CGRect(x: 528, y: 527, width: 68, height: 30)
        static let popup = CGRect(x: 518, y: 364, width: 44, height: 150)
        /// The cover above and the button row below the box.
        static let hitBounds = CGRect(x: -36, y: -84, width: 632, height: 654)
    }

    private let controller: NowPlayingController
    private let audio: AudioDeviceController
    private let perAppAudio: PerAppAudioController
    private let now: () -> TimeInterval
    private let scheduleDisplayUpdate: DisplaySchedule
    private var observers: (player: UUID?, audio: UUID?, routes: UUID?) = (nil, nil, nil)
    private struct Structure: Equatable { let language: AppLanguage; let playing: Bool }
    private var renderedStructure: Structure?
    /// Source and title of the track on the page; another one is a track change.
    private var shownTrack: String?
    /// Previous enters from the left; anything else (Next, the end of a track) from the right.
    private var pendingDirection: (dx: CGFloat, until: TimeInterval)?
    private var cancelClock: (() -> Void)?
    private var clockDeadline: TimeInterval?
    private var clockGeneration = 0
    private var requestedEndRefresh = false
    private var lyricsVisible = false
    /// Gestures belong to the player (and track) on which they began.
    private var seekGesture: (application: NowPlayingApplication?, track: NowPlayingTrack)?
    private var volumeGesture: (application: NowPlayingApplication?, audioID: UInt32?)?
    private var displayedArt: CGImage?
    // Elements of the current page, updated in place between rebuilds.
    private var art: CALayer?
    private var placeholder: CALayer?
    private var titleText: HUDTextLayer?
    private var artistText: HUDTextLayer?
    private var elapsedText: HUDTextLayer?
    private var durationText: HUDTextLayer?
    private var lyricWindow: NowPlayingLyricWindow?
    private var seek: NowPlayingSeekBar?
    private var playButton: NowPlayingPlayButton?
    private var previousButton: HUDIconButton?
    private var nextButton: HUDIconButton?
    private var volumeButton: HUDIconButton?
    private var lyricsButton: HUDIconButton?
    private var popup: NowPlayingVolumePopup?
    private var trackReadout: NowPlayingTrackReadout?

    init(context: HUDDesignContext, controller: NowPlayingController, audio: AudioDeviceController,
         perAppAudio: PerAppAudioController, now: @escaping () -> TimeInterval = { ProcessInfo.processInfo.systemUptime },
         scheduleDisplayUpdate: DisplaySchedule? = nil) {
        self.controller = controller; self.audio = audio; self.perAppAudio = perAppAudio; self.now = now
        self.scheduleDisplayUpdate = scheduleDisplayUpdate ?? { delay, action in
            let timer = Timer(timeInterval: delay, repeats: false) { _ in action() }
            timer.tolerance = min(0.01, delay * 0.1)
            RunLoop.main.add(timer, forMode: .common)
            return { timer.invalidate() }
        }
        super.init(module: .nowPlaying, context: context)
        showsHeader = false
        controller.onEvent = { [weak self] event in self?.onEvent?(event) }
        observers = (controller.observe { [weak self] in self?.refresh() }, audio.observe { [weak self] in self?.refresh() },
                     perAppAudio.observe { [weak self] in self?.refresh() })
    }

    deinit {
        cancelClock?()
        if let id = observers.player { controller.removeObserver(id) }
        if let id = observers.audio { audio.removeObserver(id) }
        if let id = observers.routes { perAppAudio.removeObserver(id) }
        if isPresented { controller.deactivate(); audio.stop() }
    }

    // MARK: Presentation

    /// Player observation and the shared audio watch run from the start of the
    /// HUD's reveal, so the cover is ready when the page enters, until it hides.
    func setPresented(_ value: Bool) {
        guard value != isPresented else { return }
        isPresented = value
        if value {
            presentationStartCountForVerification += 1
            audio.start(); controller.activate(); refresh()
        } else {
            stopClock(); closeVolume(); seek?.cancelDrag()
            requestedEndRefresh = false
            // Nothing keeps running on the hidden page (bar runs, lyric slides, fades).
            Self.removeAnimations(in: page.layer)
            controller.deactivate(); audio.stop()
        }
    }

    override func recordingModeChanged() {}

    override func renderScaleChanged() { closeVolume(); super.renderScaleChanged() }

    override func closePopups() { super.closePopups(); closeVolume() }

    private static func removeAnimations(in layer: CALayer) {
        layer.removeAllAnimations(); layer.sublayers?.forEach(removeAnimations)
    }

    private var structure: Structure { Structure(language: L10n.resolvedLanguage, playing: controller.snapshot.track != nil) }

    private func refresh() {
        guard isPresented else { return }
        let snapshot = controller.snapshot
        if let gesture = seekGesture, gesture.application != snapshot.application
            || snapshot.track.map({ !$0.hasSameIdentity(as: gesture.track) }) ?? true { seek?.cancelDrag() }
        if let gesture = volumeGesture, gesture.application != snapshot.application { popup?.clearInteractionState(); volumeGesture = nil }
        let identity = snapshot.track.map { (snapshot.application?.source.rawValue ?? "") + "\u{1F}" + $0.title }
        if identity != shownTrack { requestedEndRefresh = false }
        if let duration = snapshot.track?.duration, let elapsed = snapshot.track?.elapsed(at: now()), elapsed < duration - 1 {
            requestedEndRefresh = false
        }
        let previous = shownTrack
        shownTrack = identity
        guard renderedLanguage != nil else { return }
        if structure != renderedStructure { closeVolume(); rerender() }
        else if identity != previous, isActive, root.superlayer != nil { navigate(dx: takeDirection()) }
        else if identity != previous { rerender() }
        else { update(animated: true) }
    }

    private func takeDirection() -> CGFloat {
        defer { pendingDirection = nil }
        guard let pending = pendingDirection, pending.until > now() else { return 1 }
        return pending.dx
    }

    // MARK: Page

    override func buildPage(_ page: HUDDesignPage) {
        renderedStructure = structure
        controller.artworkPixelSize = NowPlayingArtworkLoader.pixelSize(points: Layout.cover.width, scale: env.renderScale)
        isVolumeOpen = false; popup = nil; volumeGesture = nil; displayedArt = nil; lyricsVisible = false
        let track = controller.snapshot.track
        let cover = buildCover(track)
        let lyrics = NowPlayingLyricWindow(frame: Layout.lyrics, env: env)
        lyricWindow = lyrics
        let timeline = buildTimeline()
        let transport = HUDLayer.plain("nowPlaying.transport"), extras = HUDLayer.plain("nowPlaying.extras")
        let previous = HUDIconButton(id: "previous", label: L10n.text("Previous track", "上一首"), icon: .prev, round: true, big: true, env: env)
        let play = NowPlayingPlayButton(env: env)
        let next = HUDIconButton(id: "next", label: L10n.text("Next track", "下一首"), icon: .next, round: true, big: true, env: env)
        let volume = HUDIconButton(id: "volume", label: L10n.text("App volume", "应用音量"), icon: .vol, env: env)
        let lyricsToggle = HUDIconButton(id: "lyrics", label: L10n.text("Lyrics", "歌词"), icon: .lyric, env: env)
        previous.onActivate = { [weak self] in self?.previousTrack() }
        play.onActivate = { [weak self] in self?.controller.perform(.playPause) }
        next.onActivate = { [weak self] in self?.nextTrack() }
        volume.onActivate = { [weak self] in self?.toggleVolume() }
        lyricsToggle.onActivate = { [weak self] in self?.toggleLyrics() }
        HUDMotion.perform {
            transport.frame = Layout.transport; extras.frame = Layout.extras
            previous.layer.frame.origin = CGPoint(x: 0, y: 5)
            play.layer.frame.origin = CGPoint(x: 54, y: 0)
            next.layer.frame.origin = CGPoint(x: 118, y: 5)
            volume.layer.frame.origin = .zero
            lyricsToggle.layer.frame.origin = CGPoint(x: 38, y: 0)
            [previous.layer, play.layer, next.layer].forEach(transport.addSublayer)
            [volume.layer, lyricsToggle.layer].forEach(extras.addSublayer)
            [cover, lyrics.layer, timeline, transport, extras].forEach(page.layer.addSublayer)
        }
        [cover, lyrics.layer, timeline, transport, extras].forEach(page.stagger)
        let readout = NowPlayingTrackReadout(cover: cover, track: track, env: env)
        trackReadout = readout
        page.add(readout)
        if let seek { page.add(seek) }
        page.add([previous, play, next, volume, lyricsToggle])
        previousButton = previous; playButton = play; nextButton = next; volumeButton = volume; lyricsButton = lyricsToggle
        lyrics.setLyrics(track == nil ? nil : controller.lyrics)
        update(animated: false)
    }

    /// The cover (spec §3.12 Cover): art with the title shade, or the hatched
    /// nothing-playing plate. The ring also stands in for missing artwork.
    private func buildCover(_ track: NowPlayingTrack?) -> CALayer {
        let cover = HUDLayer.plain("nowPlaying.cover")
        let size = Layout.cover.size, bounds = CGRect(origin: .zero, size: size)
        let ring = HUDLayer.plain("nowPlaying.cover.ring")
        HUDMotion.perform {
            cover.frame = Layout.cover; cover.masksToBounds = true
            cover.backgroundColor = HUDColor.counterBackground
            ring.bounds = CGRect(x: 0, y: 0, width: 72, height: 72); ring.cornerRadius = 36
            ring.borderWidth = 2; ring.borderColor = HUDColor.rgb(0x999999, 0.4)
        }
        guard let track else {
            let hatch = HUDLayer.shape("nowPlaying.cover.hatch")
            let label = HUDTextLayer(L10n.text("No music playing", "未在播放音乐"), style: HUDType.label, alignment: .center)
            HUDMotion.perform {
                hatch.frame = bounds; hatch.path = Self.hatch(size); hatch.fillColor = HUDColor.white(0.14)
                // 72 px ring, 14 px gap and the 10 px label, centred as one block.
                ring.position = CGPoint(x: size.width / 2, y: (size.height - 96) / 2 + 36)
                label.lineFrame = CGRect(x: 0, y: (size.height - 96) / 2 + 86, width: size.width, height: 10)
                [hatch, ring, label].forEach(cover.addSublayer)
            }
            art = nil; placeholder = nil; titleText = nil; artistText = nil
            return cover
        }
        let image = HUDLayer.plain("nowPlaying.cover.art")
        let shade = HUDLayer.gradient("nowPlaying.cover.shade")
        let title = HUDTextLayer(track.title, style: HUDTextStyle(size: 21, weight: .bold, color: HUDColor.white(1), lineHeight: 1.2))
        let artist = HUDTextLayer(track.artist, style: HUDTextStyle(size: 14, weight: .regular, color: HUDColor.white(0.8), lineHeight: 1.2))
        HUDMotion.perform {
            image.frame = bounds; image.contentsGravity = .resizeAspectFill
            ring.position = CGPoint(x: size.width / 2, y: size.height / 2)
            shade.frame = CGRect(x: 0, y: size.height - 116, width: size.width, height: 116)
            shade.colors = [HUDColor.black(0), HUDColor.black(0.76), HUDColor.black(0.9)]
            shade.locations = [0, 0.3, 1]
            title.lineFrame = CGRect(x: 20, y: size.height - 38 - 21 * 1.2, width: size.width - 40, height: 21 * 1.2)
            artist.lineFrame = CGRect(x: 20, y: size.height - 16 - 14 * 1.2, width: size.width - 40, height: 14 * 1.2)
            [image, ring, shade, title, artist].forEach(cover.addSublayer)
        }
        art = image; placeholder = ring; titleText = title; artistText = artist
        return cover
    }

    /// The dark seek bar with the elapsed and total times under it (spec §3.12 Seek).
    private func buildTimeline() -> CALayer {
        let group = HUDLayer.plain("nowPlaying.timeline")
        let bar = NowPlayingSeekBar(width: Layout.timeline.width, env: env)
        bar.bubbleHost = top
        bar.onScrub = { [weak self] value in self?.scrub(to: value) }
        bar.onRelease = { [weak self] value in self?.endScrub(at: value) }
        let style = HUDTextStyle(size: 10.5, weight: .medium, color: HUDColor.tone(0x999999), lineHeight: 1.2, tabular: true)
        let elapsed = HUDTextLayer(style: style), total = HUDTextLayer(style: style, alignment: .right)
        HUDMotion.perform {
            group.frame = Layout.timeline
            bar.layer.frame.origin = .zero
            // Times: margin-top −2 under the 22 px bar.
            elapsed.lineFrame = CGRect(x: 0, y: 20, width: 200, height: 10.5 * 1.2)
            total.lineFrame = CGRect(x: Layout.timeline.width - 200, y: 20, width: 200, height: 10.5 * 1.2)
            [bar.layer, elapsed, total].forEach(group.addSublayer)
        }
        seek = bar; elapsedText = elapsed; durationText = total
        return group
    }

    /// Hatch-L (spec §1.6) as one vector path: 1 px stripes 3.5 px apart at −45°.
    private static func hatch(_ size: CGSize) -> CGPath {
        let path = CGMutablePath(), spacing = 3.5 * CGFloat(2).squareRoot(), thickness = CGFloat(2).squareRoot()
        var offset = -size.height
        while offset < size.width {
            path.addLines(between: [CGPoint(x: offset, y: size.height), CGPoint(x: offset + thickness, y: size.height),
                                    CGPoint(x: offset + thickness + size.height, y: 0), CGPoint(x: offset + size.height, y: 0)])
            path.closeSubpath()
            offset += spacing
        }
        return path
    }

    // MARK: In-place updates

    private func update(animated: Bool) {
        let snapshot = controller.snapshot, track = snapshot.track
        let transport = track != nil && snapshot.failure == nil
        let playLabel = playButton?.label
        playButton?.isPlaying = track?.isPlaying == true
        [playButton, previousButton, nextButton].forEach { $0?.isEnabled = transport }
        volumeButton?.isEnabled = track != nil; volumeButton?.isOn = isVolumeOpen
        lyricsButton?.isEnabled = track != nil; lyricsButton?.isOn = showsLyrics
        HUDMotion.perform {
            if let track { titleText?.text = track.title; artistText?.text = track.artist; trackReadout?.track = track }
            durationText?.text = Self.time(track?.duration)
        }
        showArt(controller.artworkImage, animated: animated)
        seek?.isEnabled = canSeek
        lyricWindow?.setLyrics(track == nil ? nil : controller.lyrics)
        setLyricsVisible(showsLyrics && track != nil && controller.lyrics?.hasContent == true, animated: animated)
        renderPopup()
        updateClock(animated: animated)
        if playButton?.label != playLabel { context.onInputStateChange?() }
    }

    private var canSeek: Bool {
        guard let track = controller.snapshot.track else { return false }
        return track.supportsSeeking && track.duration != nil && track.position != nil && controller.snapshot.failure == nil
    }

    /// Art cross-fades (.2 s ease-in-out) whenever the decoded cover changes.
    private func showArt(_ image: CGImage?, animated: Bool) {
        guard let art, image !== displayedArt else { return }
        displayedArt = image
        if animated, !env.reducesMotion, art.superlayer != nil {
            let fade = CATransition()
            fade.type = .fade; fade.duration = 0.2; fade.timingFunction = HUDEasing.easeInOut
            art.add(fade, forKey: "hud.art.fade")
        }
        HUDMotion.perform { art.contents = image; placeholder?.isHidden = image != nil }
    }

    /// The whole lyric block fades (.24 s) when lyrics are toggled, arrive or end.
    private func setLyricsVisible(_ visible: Bool, animated: Bool) {
        guard let layer = lyricWindow?.layer, visible != lyricsVisible || !animated else { return }
        lyricsVisible = visible
        HUDMotion.animate(layer, "opacity", to: Float(visible ? 1 : 0), duration: animated ? 0.24 : 0, timing: HUDEasing.ease, env: env)
    }

    private var displayedElapsed: Double? {
        if let seek, seek.isDragging { return seek.value }
        return controller.snapshot.track?.elapsed(at: now())
    }

    private func updateClock(animated: Bool) {
        let elapsed = displayedElapsed
        HUDMotion.perform { elapsedText?.text = Self.time(elapsed) }
        lyricWindow?.show(at: elapsed ?? 0, animated: animated && lyricsVisible)
        reconcileClock()
    }

    /// One deadline at the next whole second or lyric cue while playing; the
    /// bar runs on to that moment on the compositor, so a tick never snaps it.
    private func reconcileClock() {
        let current = now(), track = controller.snapshot.track
        let duration = track?.duration ?? 0, dragging = seek?.isDragging == true
        guard isPresented, let track, let elapsed = track.elapsed(at: current), elapsed.isFinite else {
            stopClock(); if !dragging { seek?.show(0, duration: duration) }; return
        }
        guard track.isPlaying, track.duration.map({ elapsed < $0 }) ?? true else {
            stopClock(); if !dragging { seek?.show(elapsed, duration: duration) }; return
        }
        var delay = floor(elapsed) + 1 - elapsed
        if lyricsVisible, !dragging, let cue = controller.lyrics?.nextBoundary(after: elapsed) { delay = min(delay, cue - elapsed) }
        if let end = track.duration { delay = min(delay, end - elapsed) }
        // Millisecond cue clusters coalesce into a bounded display cadence.
        delay = max(1.0 / 30.0, delay)
        var deadline = current + delay
        if let existing = clockDeadline, cancelClock != nil, abs(existing - deadline) < 0.0005 { deadline = existing }
        else {
            stopClock()
            clockDeadline = deadline
            let generation = clockGeneration
            cancelClock = scheduleDisplayUpdate(delay) { [weak self] in
                guard let self, self.clockGeneration == generation, self.isPresented else { return }
                self.cancelClock = nil; self.clockDeadline = nil
                self.tick()
            }
        }
        if !dragging { seek?.show(elapsed, duration: duration, toward: track.elapsed(at: deadline), over: deadline - current) }
    }

    private func stopClock() {
        clockGeneration &+= 1
        cancelClock?(); cancelClock = nil; clockDeadline = nil
    }

    private func tick() {
        guard isPresented else { return }
        updateClock(animated: true)
        // At the end the player moves on by itself; read the next track once.
        if !requestedEndRefresh, let track = controller.snapshot.track, let duration = track.duration,
           (track.elapsed(at: now()) ?? 0) >= duration {
            requestedEndRefresh = true; controller.refresh()
        }
    }

    static func time(_ value: Double?) -> String {
        guard let value, value.isFinite, value >= 0 else { return "—:—" }
        return HUDFormat.clock(seconds: Int(min(value, 604_800)))
    }

    // MARK: Transport

    /// Previous restarts the track after its first 3 s (in place); otherwise
    /// it goes to the previous track, which enters from the left.
    private func previousTrack() {
        guard let track = controller.snapshot.track else { return }
        if let elapsed = track.elapsed(at: now()), elapsed > 3, canSeek {
            controller.perform(.seek(0))
        } else {
            pendingDirection = (-1, now() + 5)
            controller.perform(.previous)
        }
    }

    private func nextTrack() {
        pendingDirection = (1, now() + 5)
        controller.perform(.next)
    }

    private func toggleLyrics() {
        showsLyrics.toggle()
        update(animated: true)
    }

    /// Dragging previews the time and lyrics locally; release seeks once.
    private func scrub(to value: Double) {
        if seekGesture == nil, let track = controller.snapshot.track {
            seekGesture = (controller.snapshot.application, track)
            stopClock()
        }
        HUDMotion.perform { elapsedText?.text = Self.time(value) }
        lyricWindow?.show(at: value, animated: lyricsVisible)
    }

    private func endScrub(at value: Double?) {
        let gesture = seekGesture
        seekGesture = nil
        let snapshot = controller.snapshot
        if let value, let gesture, gesture.application == snapshot.application,
           snapshot.track?.hasSameIdentity(as: gesture.track) == true {
            controller.perform(.seek(value))
        }
        update(animated: true)
    }

    private func skip(by seconds: Double) {
        guard canSeek, let duration = controller.snapshot.track?.duration, let elapsed = displayedElapsed else { return }
        controller.perform(.seek(min(duration, max(0, elapsed + seconds))))
    }

    // MARK: Volume pop-up

    private var currentOutput: AudioDeviceInfo? { audio.snapshot.outputs.first { $0.id == audio.snapshot.defaultOutputID } }

    /// The audio process of the playing app: only an unambiguous match, never
    /// an unrelated process or one of several helpers (the Volume module
    /// lists those individually).
    private var audioApplication: AudioApplicationInfo? {
        guard let app = controller.snapshot.application else { return nil }
        var candidates = audio.snapshot.availableApplications.isEmpty ? audio.snapshot.activeApplications : audio.snapshot.availableApplications
        for session in perAppAudio.sessions where !candidates.contains(where: { $0.id == session.processID }) {
            candidates.append(AudioApplicationInfo(id: session.processID, pid: session.pid, name: session.name,
                isRunningOutput: false, applicationURL: session.applicationURL, icon: session.icon))
        }
        let matches = candidates.filter { $0.pid == app.pid || $0.applicationURL?.standardizedFileURL == app.bundleURL.standardizedFileURL }
        return matches.count == 1 ? matches[0] : nil
    }

    private var currentSession: PerAppAudioSession? {
        guard let app = audioApplication else { return nil }
        return perAppAudio.sessions.first { $0.processID == app.id && $0.pid == app.pid }
    }

    /// The player's own volume when it exposes one, else a per-app route.
    private var volumeAvailable: Bool {
        if controller.playerVolume != nil { return true }
        guard let app = audioApplication else { return false }
        if let session = currentSession { return session.state == .active || session.state == .preparing || session.state == .failed }
        guard let output = currentOutput else { return false }
        return perAppAudio.availability(application: app, output: output) == nil
    }

    private var volumeValue: Double? {
        controller.playerVolume ?? currentSession?.gain ?? (volumeAvailable ? 1 : nil)
    }

    @discardableResult private func setAppVolume(_ value: Double) -> Bool {
        guard isPresented, value.isFinite, volumeAvailable else { return false }
        let bounded = min(1, max(0, value))
        if controller.playerVolume != nil { return controller.setPlayerVolume(bounded) }
        guard let app = audioApplication else { return false }
        if let session = currentSession {
            // Back at 100% a failed route is retired instead of retried.
            if session.state == .failed, bounded == 1 { perAppAudio.stop(processID: app.id); return true }
            return perAppAudio.setGain(bounded, processID: app.id)
        }
        // Untouched direct playback stays untouched at 100%.
        if bounded == 1 { return true }
        guard let output = currentOutput else { return false }
        return perAppAudio.start(application: app, output: output, initialGain: bounded)
    }

    private func toggleVolume() { isVolumeOpen ? closeVolume() : openVolume() }

    private func openVolume() {
        guard !isVolumeOpen, controller.snapshot.track != nil else { return }
        super.closePopups()
        let panel = NowPlayingVolumePopup(env: env)
        panel.onChange = { [weak self] value, final in self?.adjustVolume(value, final: final) }
        HUDMotion.perform {
            panel.layer.frame = Layout.popup; panel.layer.zPosition = 5
            page.layer.addSublayer(panel.layer)
        }
        env.applyScale(to: panel.layer)
        page.add(panel)
        popup = panel; isVolumeOpen = true
        renderPopup()
        volumeButton?.isOn = true
        HUDMotion.wipe(panel.layer, from: .top, duration: HUDDuration.menuWipe, fadeFrom: 0.4, env: env)
        refreshRouter(); context.onInputStateChange?()
    }

    private func closeVolume() {
        guard isVolumeOpen else { return }
        isVolumeOpen = false; volumeGesture = nil
        if let popup {
            popup.clearInteractionState()
            page.remove(popup)
            HUDMotion.perform { popup.layer.removeFromSuperlayer() }
        }
        popup = nil
        volumeButton?.isOn = false
        refreshRouter(); context.onInputStateChange?()
    }

    private func adjustVolume(_ value: Double, final: Bool) {
        if volumeGesture == nil { volumeGesture = (controller.snapshot.application, audioApplication?.id) }
        if let gesture = volumeGesture, gesture.application == controller.snapshot.application, gesture.audioID == audioApplication?.id {
            setAppVolume(value)
        }
        if final { volumeGesture = nil; renderPopup() }
    }

    private func renderPopup() {
        guard let popup, !popup.isDragging else { return }
        popup.isEnabled = volumeAvailable
        popup.show(volumeValue)
    }

    // MARK: Input

    /// A press outside the pop-up and its button only closes the pop-up (spec §1.12).
    @discardableResult
    override func pointerDown(at point: CGPoint, event: NSEvent? = nil) -> Bool {
        if isVolumeOpen, isActive, !isTransitioning, !isMenuOpen, !isPopoverOpen, !Layout.popup.contains(point),
           !(volumeButton.map { $0.rect(in: root).contains(point) } ?? false) {
            closeVolume(); return true
        }
        return super.pointerDown(at: point, event: event)
    }

    /// Space plays or pauses, ← / → seek 5 s, Esc closes the volume pop-up.
    override func handleKey(_ event: NSEvent) -> Bool {
        guard event.modifierFlags.intersection([.command, .control, .option, .shift]).isEmpty else { return false }
        switch event.keyCode {
        case 53:
            guard isVolumeOpen else { return false }
            closeVolume()
        case 49:
            guard playButton?.isEnabled == true else { return false }
            controller.perform(.playPause)
        case 123, 124:
            guard canSeek else { return false }
            skip(by: event.keyCode == 123 ? -5 : 5)
        default: return false
        }
        return true
    }

    override func adjustAccessibilitySlider(_ id: String, value: Double) {
        switch id {
        case "seek": if canSeek { controller.perform(.seek(value)) }
        case "appVolume": setAppVolume(value / 100)
        default: super.adjustAccessibilitySlider(id, value: value)
        }
    }

    /// Previous, play / pause, next, volume or lyrics, as a press would.
    func perform(actionID: String) { performAccessibilityAction(actionID) }

    /// Buttons and sliders of the page (box px), for the UI verification run.
    var accessibleActions: [HUDAccessibilityItem] { controlItems.filter { $0.role == .button } }
    var accessibleSliders: [HUDAccessibilityItem] { controlItems.filter { if case .slider = $0.role { return true }; return false } }
    private var controlItems: [HUDAccessibilityItem] {
        page.controls.filter { $0.layer.isVisible(in: root) }.flatMap { $0.accessibilityItems(in: root) }
    }
}

/// The track for VoiceOver (title / artist / album, or nothing playing) on
/// the cover; it never takes a press.
private final class NowPlayingTrackReadout: HUDControl {
    var track: NowPlayingTrack?
    init(cover: CALayer, track: NowPlayingTrack?, env: HUDDesignEnvironment) {
        self.track = track
        super.init(id: "track", label: HUDModule.nowPlaying.title, env: env, layer: cover)
    }
    override func contains(_ point: CGPoint) -> Bool { false }
    override var accessibilityRole: HUDAccessibilityItem.Role? {
        .text(track.map { [$0.title, $0.artist, $0.album].filter { !$0.isEmpty }.joined(separator: " / ") }
              ?? L10n.text("No music playing", "未在播放音乐"))
    }
}

/// The three-line lyric window (spec §3.12 Lyrics): line k sits in the middle
/// slot at 14 px, its neighbours at 12.5 px and .36, the rest hidden. Only
/// k ± 2 have layers; a cue change slides them one slot (.22 s ease-in-out)
/// and eases opacity and size.
private final class NowPlayingLyricWindow {
    static let lineHeight: CGFloat = 20
    static var regular: HUDTextStyle { HUDTextStyle(size: 12.5, weight: .regular, color: HUDColor.tone(0xF0F0F0)) }
    static var current: HUDTextStyle { HUDTextStyle(size: 14, weight: .medium, color: HUDColor.tone(0xF0F0F0)) }
    let layer = HUDLayer.plain("nowPlaying.lyrics")
    private let env: HUDDesignEnvironment
    private var lyrics: NowPlayingLyrics?
    private var index: Int?
    private var rows: [Int: HUDTextLayer] = [:]

    init(frame: CGRect, env: HUDDesignEnvironment) {
        self.env = env
        HUDMotion.perform { layer.frame = frame; layer.masksToBounds = true; layer.opacity = 0 }
    }

    func setLyrics(_ next: NowPlayingLyrics?) {
        guard next != lyrics else { return }
        lyrics = next; index = nil
        HUDMotion.perform { rows.values.forEach { $0.removeFromSuperlayer() } }
        rows = [:]
    }

    func show(at elapsed: Double, animated: Bool) {
        guard let lyrics else { return }
        let next = lyrics.currentIndex(at: elapsed)
        guard next != index else { return }
        let previous = index
        index = next
        let slide = animated && !env.reducesMotion && previous.map { abs($0 - next) <= 2 } == true
        let wanted = ((next - 2)...(next + 2)).filter { lyrics.lines.indices.contains($0) }
        for (line, row) in rows where !wanted.contains(line) {
            HUDMotion.perform { row.removeFromSuperlayer() }
            rows[line] = nil
        }
        for line in wanted {
            let existing = rows[line]
            let row = existing ?? HUDTextLayer(lyrics.lines[line].text, style: Self.regular, alignment: .center)
            let distance = abs(line - next)
            let style = distance == 0 ? Self.current : Self.regular
            let opacity: Float = distance == 0 ? 1 : (distance == 1 ? 0.36 : 0)
            // Where the line was before this cue (a new row starts there, hidden).
            let fromY = existing.map { ($0.presentation() ?? $0).position.y }
                ?? previous.map { Self.lineHeight * CGFloat(line - $0 + 1) + Self.lineHeight / 2 }
            let fromOpacity = existing.map { ($0.presentation() ?? $0).opacity } ?? 0
            let fromSize = existing?.textStyle.size ?? style.size
            HUDMotion.perform {
                row.textStyle = style
                row.lineFrame = CGRect(x: 0, y: Self.lineHeight * CGFloat(line - next + 1), width: layer.bounds.width, height: Self.lineHeight)
                row.opacity = opacity
                if existing == nil { layer.addSublayer(row) }
            }
            if existing == nil { env.applyScale(to: row); rows[line] = row }
            guard slide, let fromY else { continue }
            add(row, "position.y", from: fromY, to: row.position.y)
            add(row, "opacity", from: fromOpacity, to: opacity)
            if fromSize != style.size { add(row, "transform.scale", from: fromSize / style.size, to: 1) }
        }
    }

    private func add(_ row: CALayer, _ keyPath: String, from: Any, to: Any) {
        let motion = CABasicAnimation(keyPath: keyPath)
        motion.fromValue = from; motion.toValue = to
        motion.duration = 0.22; motion.timingFunction = HUDEasing.easeInOut
        row.add(motion, forKey: "hud.lyric." + keyPath)
    }
}

/// The Now Playing timeline (spec §3.12 Seek): the dark slider at 22 px with
/// a 3 px track, a 13 px thumb and no ladder. Dragging previews (the bubble
/// shows mm:ss); release commits once. Between clock ticks the bar runs on
/// linearly on the compositor.
private final class NowPlayingSeekBar: HUDControl {
    let width: CGFloat
    private let track = HUDLayer.plain("seek.track")
    private let fill = HUDLayer.gradient("seek.fill")
    private let thumb = HUDLayer.plain("seek.thumb")
    private var bubble: HUDSliderBubble?
    weak var bubbleHost: CALayer?
    private(set) var value: Double = 0
    private(set) var duration: Double = 0
    private(set) var isDragging = false
    var onScrub: ((Double) -> Void)?
    /// The released value, or nil when the gesture was interrupted.
    var onRelease: ((Double?) -> Void)?

    init(width: CGFloat, env: HUDDesignEnvironment) {
        self.width = width
        super.init(id: "seek", label: L10n.text("Playback position", "播放进度"), env: env)
        HUDMotion.perform {
            layer.bounds = CGRect(x: 0, y: 0, width: width, height: 22)
            track.frame = CGRect(x: 0, y: 8, width: width, height: 3); track.backgroundColor = HUDColor.ringField
            fill.anchorPoint = CGPoint(x: 0, y: 0.5)
            fill.bounds = CGRect(x: 0, y: 0, width: 0, height: 3); fill.position = CGPoint(x: 0, y: 9.5)
            fill.startPoint = CGPoint(x: 0, y: 0.5); fill.endPoint = CGPoint(x: 1, y: 0.5)
            fill.colors = [HUDColor.legible(HUDColor.yellowLow), HUDColor.accentInk]
            thumb.bounds = CGRect(x: 0, y: 0, width: 9, height: 13); thumb.position = CGPoint(x: 0, y: 9.5)
            thumb.backgroundColor = HUDColor.tone(0xEDEDED)
            let core = HUDLayer.plain("seek.core")
            core.frame = CGRect(x: 3.5, y: 3, width: 2, height: 7); core.backgroundColor = HUDColor.tone(0x111111)
            thumb.addSublayer(core)
            [track, fill, thumb].forEach(layer.addSublayer)
        }
        setTarget(CGRect(x: -5, y: -3, width: width + 10, height: 28))
    }

    deinit { bubble?.layer.removeFromSuperlayer() }

    private func x(_ seconds: Double) -> CGFloat {
        duration > 0 ? CGFloat(min(1, max(0, seconds / duration))) * width : 0
    }

    /// Shows `seconds`; while playing the bar runs on to `destination` over `interval`.
    func show(_ seconds: Double, duration total: Double, toward destination: Double? = nil, over interval: TimeInterval = 0) {
        guard !isDragging else { return }
        value = seconds; duration = max(0, total)
        place(x(seconds), toward: destination.map(x), over: interval)
    }

    private func place(_ start: CGFloat, toward end: CGFloat? = nil, over interval: TimeInterval = 0) {
        fill.removeAnimation(forKey: "hud.seek.progress"); thumb.removeAnimation(forKey: "hud.seek.progress")
        let target = end ?? start
        HUDMotion.perform { fill.bounds.size.width = target; thumb.position.x = target }
        guard target != start, interval > 0, !env.reducesMotion, layer.superlayer != nil else { return }
        for (item, keyPath) in [(fill as CALayer, "bounds.size.width"), (thumb, "position.x")] {
            let run = CABasicAnimation(keyPath: keyPath)
            run.fromValue = start; run.toValue = target
            run.duration = interval; run.timingFunction = HUDEasing.linear
            item.add(run, forKey: "hud.seek.progress")
        }
    }

    override func pointerDown(at point: CGPoint) -> Bool {
        guard isEnabled, duration > 0 else { return false }
        isDragging = true
        scrub(to: point)
        showBubble()
        return true
    }
    override func pointerDragged(to point: CGPoint) { if isDragging { scrub(to: point) } }
    override func pointerUp(at point: CGPoint) {
        guard isDragging else { return }
        isDragging = false; bubble?.setVisible(false)
        onRelease?(value)
    }
    /// An interrupted drag (track change, module change) ends without a seek.
    func cancelDrag() {
        guard isDragging else { return }
        isDragging = false; bubble?.setVisible(false)
        onRelease?(nil)
    }
    override func clearInteractionState() { super.clearInteractionState(); cancelDrag() }

    private func scrub(to point: CGPoint) {
        value = Double(min(1, max(0, point.x / width))) * duration
        place(x(value))
        updateBubble()
        onScrub?(value)
    }

    private func showBubble() {
        guard let host = bubbleHost else { return }
        let bubble = self.bubble ?? HUDSliderBubble(env: env)
        self.bubble = bubble
        if bubble.layer.superlayer !== host { HUDMotion.perform { host.addSublayer(bubble.layer) } }
        updateBubble()
        bubble.setVisible(true)
    }

    private func updateBubble() {
        guard isDragging, let bubble, let host = bubbleHost else { return }
        bubble.show(NowPlayingCanvas.time(value), anchor: host.convert(CGPoint(x: x(value), y: 2), from: layer))
    }

    override var accessibilityRole: HUDAccessibilityItem.Role? {
        .slider(value: value, minimum: 0, maximum: max(1, duration), step: 5, description: NowPlayingCanvas.time(duration > 0 ? value : nil))
    }
}

/// The vertical volume pop-up (spec §3.12): `70%` label, 6 px track, yellow
/// fill from the bottom and a 16 × 6 knob. A press or drag sets 1 − y / height.
private final class NowPlayingVolumePopup: HUDControl {
    static let track = CGRect(x: 19, y: 30, width: 6, height: 106)
    private let text = HUDTextLayer(style: HUDTextStyle(size: 10, weight: .bold, color: HUDColor.textOnDark, tabular: true), alignment: .center)
    private let fill = HUDLayer.plain("volume.fill")
    private let knob = HUDLayer.plain("volume.knob")
    private(set) var value: Double?
    private(set) var isDragging = false
    /// (value 0…1, final): final on release.
    var onChange: ((Double, Bool) -> Void)?

    init(env: HUDDesignEnvironment) {
        super.init(id: "appVolume", label: L10n.text("Playing app volume", "当前播放应用音量"), env: env)
        let bounds = CGRect(x: 0, y: 0, width: 44, height: 150)
        HUDMotion.perform {
            layer.bounds = bounds
            layer.backgroundColor = HUDColor.panel; layer.borderWidth = 1; layer.borderColor = HUDColor.ringMenu
            layer.shadowColor = HUDColor.black(1); layer.shadowOpacity = 0.6; layer.shadowRadius = 20
            layer.shadowOffset = CGSize(width: 0, height: 18); layer.shadowPath = CGPath(rect: bounds, transform: nil)
            text.lineFrame = CGRect(x: 0, y: 9, width: bounds.width, height: 10)
            let rail = HUDLayer.plain("volume.track")
            rail.frame = Self.track; rail.backgroundColor = HUDColor.separator
            fill.backgroundColor = HUDColor.accentInk
            knob.bounds = CGRect(x: 0, y: 0, width: 16, height: 6); knob.backgroundColor = HUDColor.textOnDark
            [text, rail, fill, knob].forEach(layer.addSublayer)
        }
        setTarget(bounds)
    }

    func show(_ next: Double?) {
        value = next
        let height = Self.track.height * CGFloat(min(1, max(0, next ?? 0)))
        HUDMotion.perform {
            text.text = next.map { "\(Int(($0 * 100).rounded()))%" } ?? "—"
            fill.frame = CGRect(x: Self.track.minX, y: Self.track.maxY - height, width: Self.track.width, height: height)
            knob.position = CGPoint(x: Self.track.midX, y: Self.track.maxY - height)
            knob.isHidden = next == nil
        }
    }

    override func pointerDown(at point: CGPoint) -> Bool {
        guard isEnabled else { return false }
        isDragging = true; select(at: point)
        return true
    }
    override func pointerDragged(to point: CGPoint) { if isDragging { select(at: point) } }
    override func pointerUp(at point: CGPoint) {
        guard isDragging else { return }
        isDragging = false
        onChange?(value ?? 0, true)
    }
    override func clearInteractionState() { super.clearInteractionState(); isDragging = false }
    override func enabledChanged() { HUDMotion.perform { fill.opacity = isEnabled ? 1 : 0.35; knob.opacity = isEnabled ? 1 : 0.35 } }

    private func select(at point: CGPoint) {
        let next = (Double(min(1, max(0, 1 - (point.y - Self.track.minY) / Self.track.height))) * 100).rounded() / 100
        show(next)
        onChange?(next, false)
    }

    override var accessibilityRole: HUDAccessibilityItem.Role? {
        .slider(value: (value ?? 0) * 100, minimum: 0, maximum: 100, step: 2, description: text.text)
    }
}

/// The yellow 54 px play / pause circle (spec §3.12 Transport): hover scales
/// it 1.06 and brightens it, a press pushes it to .95.
private final class NowPlayingPlayButton: HUDControl {
    private let face = HUDLayer.gradient("play.face")
    private let brighten = HUDLayer.plain("play.brighten")
    private let glyph = HUDIconLayer(.play, size: 22, color: HUDColor.nearBlack)
    var isPlaying = false {
        didSet {
            guard isPlaying != oldValue else { return }
            HUDMotion.perform { glyph.set(isPlaying ? .pause : .play, size: 22) }
            label = isPlaying ? L10n.text("Pause", "暂停") : L10n.text("Play", "播放")
        }
    }

    init(env: HUDDesignEnvironment) {
        super.init(id: "playPause", label: L10n.text("Play", "播放"), env: env)
        let bounds = CGRect(x: 0, y: 0, width: 54, height: 54)
        HUDMotion.perform {
            layer.bounds = bounds
            face.frame = bounds; face.cornerRadius = 27
            face.colors = HUDColor.primaryButton; face.locations = [0, 0.5, 1]
            brighten.frame = bounds; brighten.cornerRadius = 27; brighten.backgroundColor = HUDColor.white(0.06); brighten.opacity = 0
            glyph.position = CGPoint(x: 27, y: 27)
            [face, brighten, glyph].forEach(layer.addSublayer)
        }
        setTarget(bounds, path: CGPath(ellipseIn: bounds, transform: nil))
    }

    override func hoverChanged() { applyHoverMotion(hoverScale: 1.06, pressScale: 0.95, brighten: brighten) }
    override func pressChanged() { applyHoverMotion(hoverScale: 1.06, pressScale: 0.95, brighten: brighten) }
    override func enabledChanged() { HUDMotion.perform { layer.opacity = isEnabled ? 1 : 0.35 }; hoverChanged() }
}
