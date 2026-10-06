import AppKit
import QuartzCore

/// Retained cover, lyrics and controls in the HUD plane. Routine metadata and
/// transport updates never rebuild the panel or replay a whole-panel reveal.
/// One visible-only deadline advances progress at whole seconds and lyrics at
/// their own cue boundaries. It interpolates locally without sampling a player.
final class NowPlayingCanvas: NSObject, HUDModuleContentFactory {
    let layer = CALayer()
    var onChange: (() -> Void)?
    var onEvent: ((NowPlayingEvent) -> Void)?
    var isDragging: Bool { draggedSlider != nil }
    var isRequestingPermission: Bool { controller.isRequestingPermission }
    var progressClockActiveForVerification: Bool { cancelClock != nil }
    var albumCoverAvailableForVerification: Bool { controller.artworkImage != nil }
    func tickForVerification() { tick() }
    private let controller: NowPlayingController
    private let audio: AudioDeviceController
    private let perAppAudio: PerAppAudioController
    private let now: () -> TimeInterval
    private let reduceMotion: () -> Bool
    private var observer: UUID?
    private var audioObserver: UUID?
    private var routeObserver: UUID?
    private var active = false
    private var dark = true
    private var scale: CGFloat = 2
    typealias DisplaySchedule = (TimeInterval, @escaping () -> Void) -> (() -> Void)
    private let scheduleDisplayUpdate: DisplaySchedule
    private var cancelClock: (() -> Void)?
    private var clockDeadline: TimeInterval?
    private var clockGeneration = 0
    private var requestedEndRefresh = false
    private var trackIdentity = ""
    private var draggedSlider: String?
    private var dragValue: Double?
    private var draggedApplication: NowPlayingApplication?
    private var draggedAudioID: UInt32?
    private var draggedTrack: NowPlayingTrack?
    private let content = CALayer()
    private let panel = CALayer()
    private let titleLabel = CATextLayer(), artistLabel = CATextLayer(), durationLabel = CATextLayer()
    private let emptyLabel = CATextLayer()
    private let metadataShade = CAGradientLayer()
    private let progressRail = CALayer()
    private let coverPlaceholder = CAShapeLayer()
    private let lyricsLabels = [CATextLayer(), CATextLayer(), CATextLayer()]
    private let lyricsViewport = CALayer()
    private var lastLyricPosition: TimeInterval?
    private let controls = CALayer()
    private var buttonLayers: [String: NowPlayingButtonArtwork] = [:]
    private let volumeMenu = CALayer(), volumeBacking = CALayer(), volumeFace = CALayer()
    private let volumeRail = CALayer(), volumeFill = CALayer(), volumeHandle = CALayer()
    private let volumeLabel = CATextLayer()
    private var showVolume = false
    private var showLyrics = true
    private var lastLyricsVisible = false
    private var rendered = false
    var capturesPointer: Bool { showVolume }
    private var lyricsVisible: Bool { showLyrics && controller.lyrics?.hasContent == true }
    // Reserve lyric space even while hidden. Transport, seek and volume input
    // must never jump beneath the pointer when lyric visibility changes.
    private let controlY: CGFloat = 406
    private var volumeBounds: CGRect { CGRect(x: 357, y: controlY - 188, width: 53, height: 180) }
    var panelFrameForVerification: CGRect { panel.frame }

    private let albumCover = CALayer()
    private var displayedCover: CGImage?
    private let progressFill = CALayer()
    private let progressHandle = CALayer()
    private let elapsedLabel = CATextLayer()
    private var snapshot: NowPlayingSnapshot { controller.snapshot }
    private var accent: NSColor { HUDRuntimeAppearance.accent }
    private var primary: NSColor { NSColor(white: dark ? 0.94 : 0.12, alpha: 1) }
    private var muted: NSColor { NSColor(white: dark ? 0.60 : 0.40, alpha: 1) }
    private var seekRect: CGRect { CGRect(x: 55, y: 379, width: 330, height: 18) }
    private var volumeRect: CGRect { CGRect(x: 371, y: volumeBounds.minY + 34, width: 25, height: 117) }

    init(controller: NowPlayingController, audio: AudioDeviceController,
         perAppAudio: PerAppAudioController, now: @escaping () -> TimeInterval = { ProcessInfo.processInfo.systemUptime },
         reduceMotion: @escaping () -> Bool = { HUDRuntimeAppearance.reduceMotion },
         scheduleDisplayUpdate: DisplaySchedule? = nil) {
        self.controller = controller; self.audio = audio; self.perAppAudio = perAppAudio; self.now = now; self.reduceMotion = reduceMotion
        self.scheduleDisplayUpdate = scheduleDisplayUpdate ?? { delay, action in
            let timer = Timer(timeInterval: delay, repeats: false) { _ in action() }
            timer.tolerance = min(0.01, delay * 0.1)
            RunLoop.main.add(timer, forMode: .common)
            return { timer.invalidate() }
        }
        super.init()
        withoutActions {
            layer.name = "module.nowPlaying.canvas"; layer.frame = CGRect(x: 0, y: 0, width: 440, height: 440)
            for (index, label) in ([titleLabel, artistLabel, emptyLabel, elapsedLabel, durationLabel, volumeLabel] + lyricsLabels).enumerated() {
                label.name = "nowPlaying.text.\(index)"
                label.actions = ["contents": NSNull()]
            }
            albumCover.actions = ["contents": NSNull()]
            content.frame = layer.bounds; content.allowsGroupOpacity = false; layer.addSublayer(content)
            albumCover.name = "nowPlaying.albumCover"; albumCover.contentsGravity = .resizeAspectFill
            albumCover.masksToBounds = true; albumCover.cornerRadius = 2
            panel.name = "nowPlaying.panel"; panel.anchorPoint = .zero; panel.masksToBounds = true; panel.cornerRadius = 4
            content.addSublayer(panel)
            metadataShade.name = "nowPlaying.metadataShade"
            for item in [albumCover, coverPlaceholder, metadataShade, titleLabel, artistLabel, emptyLabel] { panel.addSublayer(item) }
            lyricsViewport.name = "nowPlaying.lyrics"; lyricsViewport.masksToBounds = true
            for label in lyricsLabels { lyricsViewport.addSublayer(label) }
            for item in [lyricsViewport, progressRail, progressFill, progressHandle, elapsedLabel, durationLabel, controls, volumeMenu] { content.addSublayer(item) }
            for id in ["lyrics", "previous", "playPause", "next", "volume"] {
                let artwork = NowPlayingButtonArtwork(); artwork.layer.name = "nowPlaying.control." + id; buttonLayers[id] = artwork; controls.addSublayer(artwork.layer)
            }
            volumeMenu.name = "nowPlaying.volume.popover"; volumeMenu.isHidden = true
            for item in [volumeBacking, volumeFace, volumeRail, volumeFill, volumeHandle, volumeLabel] { volumeMenu.addSublayer(item) }
            progressFill.name = "nowPlaying.progress.fill"; progressHandle.name = "nowPlaying.progress.handle"
        }
        controller.onEvent = { [weak self] event in self?.onEvent?(event) }
        observer = controller.observe { [weak self] in self?.refresh() }
        audioObserver = audio.observe { [weak self] in self?.refresh() }
        routeObserver = perAppAudio.observe { [weak self] in self?.refresh() }
    }
    deinit {
        cancelClock?()
        if let observer { controller.removeObserver(observer) }
        if let audioObserver { audio.removeObserver(audioObserver) }
        if let routeObserver { perAppAudio.removeObserver(routeObserver) }
        if active { controller.deactivate(); audio.stop() }
    }
    func makeContent(for module: HUDModule, style: HUDModuleContentStyle) -> CALayer {
        dark = style.dark; scale = style.contentsScale; repaint(); return layer
    }
    func updateRenderScale(_ value: CGFloat) {
        let next = value.isFinite ? min(8, max(1, value)) : 2
        guard next != scale else { return }; scale = next
        if active { repaint() }
    }
    func activate() {
        guard !active else { return }; active = true
        audio.start(); controller.activate(); refresh()
    }
    func deactivate() {
        guard active else { return }; active = false
        stopClock(); cancelDrag(); showVolume = false; volumeMenu.isHidden = true
        controller.deactivate(); audio.stop()
        content.removeAllAnimations(); progressFill.removeAllAnimations(); progressHandle.removeAllAnimations()
        removeAnimations(in: content)
        // Keep the already decoded thumbnail for the outgoing HUD plane. It
        // shares the bounded artwork cache and is replaced on the next repaint;
        // no decoder, observer or animation remains active while hidden.
        lastLyricPosition = nil
    }

    var accessibilityStatus: String {
        guard let track = snapshot.track else { return "No music playing" }
        return [track.title, track.artist, track.album].filter { !$0.isEmpty }.joined(separator: " · ")
    }
    var accessibleActions: [VolumeCanvasAction] {
        // In-flight reads retain usable controls. A refresh is not a disabled,
        // faded copy of the entire player.
        let enabled = snapshot.track != nil
        let transportEnabled = enabled && snapshot.failure == nil
        return [
            VolumeCanvasAction(id: "lyrics", label: L10n.text("Lyrics", "歌词"), rect: CGRect(x: 404, y: controlY, width: 30, height: 30), enabled: enabled),
            VolumeCanvasAction(id: "previous", label: L10n.text("Previous track", "上一首"), rect: CGRect(x: 2, y: controlY, width: 30, height: 30), enabled: transportEnabled),
            VolumeCanvasAction(id: "playPause", label: snapshot.track?.isPlaying == true ? L10n.text("Pause", "暂停") : L10n.text("Play", "播放"), rect: CGRect(x: 38, y: controlY - 3, width: 36, height: 36), enabled: transportEnabled),
            VolumeCanvasAction(id: "next", label: L10n.text("Next track", "下一首"), rect: CGRect(x: 78, y: controlY, width: 30, height: 30), enabled: transportEnabled),
            VolumeCanvasAction(id: "volume", label: L10n.text("App volume", "应用音量"), rect: CGRect(x: 368, y: controlY, width: 30, height: 30), enabled: enabled)]
    }
    var accessibleSliders: [VolumeCanvasSlider] {
        let track = snapshot.track
        let current = draggedSlider == "seek" ? dragValue : track?.elapsed(at: now())
        let seek = VolumeCanvasSlider(id: "seek", label: L10n.text("Playback position", "播放进度"), rect: seekRect,
            value: current, minimum: 0, maximum: track?.duration ?? 1,
            enabled: track?.supportsSeeking == true && track?.duration != nil && track?.position != nil && snapshot.failure == nil)
        let available = volumeAvailable
        let volume = VolumeCanvasSlider(id: "appVolume", label: L10n.text("Playing app volume", "当前播放应用音量"), rect: volumeRect,
            value: controller.playerVolume ?? currentSession?.gain ?? (available ? 1 : nil), minimum: 0, maximum: 1,
            enabled: available, help: volumeHelp)
        return showVolume ? [seek, volume] : [seek]
    }
    private var currentOutput: AudioDeviceInfo? { audio.snapshot.outputs.first { $0.id == audio.snapshot.defaultOutputID } }
    private var audioApplication: AudioApplicationInfo? {
        guard let app = snapshot.application else { return nil }
        var candidates = audio.snapshot.availableApplications.isEmpty ? audio.snapshot.activeApplications : audio.snapshot.availableApplications
        for session in perAppAudio.sessions where !candidates.contains(where: { $0.id == session.processID }) {
            candidates.append(AudioApplicationInfo(id: session.processID, pid: session.pid, name: session.name,
                isRunningOutput: false, applicationURL: session.applicationURL, icon: session.icon))
        }
        let matches = candidates.filter { candidate in
            let same = candidate.pid == app.pid || candidate.applicationURL?.standardizedFileURL == app.bundleURL.standardizedFileURL
            return same
        }
        // Never adjust an unrelated process or silently choose one of several
        // helpers. The existing Volume page exposes their individual routes.
        return matches.count == 1 ? matches[0] : nil
    }
    private var currentSession: PerAppAudioSession? {
        guard let app = audioApplication else { return nil }
        return perAppAudio.sessions.first { $0.processID == app.id && $0.pid == app.pid }
    }
    private var volumeAvailable: Bool {
        if controller.playerVolume != nil { return true }
        guard let app = audioApplication else { return false }
        if let session = currentSession { return session.state == .active || session.state == .preparing || session.state == .failed }
        guard let output = currentOutput else { return false }
        return perAppAudio.availability(application: app, output: output) == nil
    }
    private var volumeHelp: String {
        if controller.playerVolume != nil { return L10n.text("Playing app volume", "当前播放应用音量") }
        if let session = currentSession { return session.error ?? L10n.text("Adjusts the playing app's existing audio route.", "调整当前播放应用的现有音频路由。") }
        if let app = audioApplication, let output = currentOutput {
            return perAppAudio.availability(application: app, output: output)
                ?? L10n.text("Adjust to enable a temporary audio route; macOS may request System Audio Recording access", "调整即可启用临时音频路由；macOS 可能请求系统音频录制权限")
        }
        return L10n.text("No single adjustable audio process. Use Volume controls.", "未找到单一可调整音频进程，请使用音量控制。")
    }

    @discardableResult func mouseDown(at point: CGPoint) -> Bool {
        guard active, point.x.isFinite, point.y.isFinite, layer.bounds.contains(point) else { return false }
        if showVolume, !volumeBounds.contains(point), !accessibleActions.contains(where: { $0.id == "volume" && $0.rect.contains(point) }) {
            dismissPopover(); return true
        }
        if let slider = accessibleSliders.first(where: { $0.rect.contains(point) }) {
            if slider.enabled {
                draggedSlider = slider.id; draggedApplication = snapshot.application; draggedTrack = snapshot.track
                draggedAudioID = audioApplication?.id; moveSlider(to: point)
            }
            return true
        }
        if let action = accessibleActions.first(where: { $0.rect.contains(point) }), action.enabled { perform(actionID: action.id) }
        return true
    }
    func mouseDragged(to point: CGPoint) { if active { moveSlider(to: point) } }
    func mouseUp() {
        guard let id = draggedSlider else { return }
        let value = dragValue
        let unchanged = snapshot.application == draggedApplication && draggedTrackIsCurrent
        cancelDrag()
        if id == "seek", unchanged, let value { controller.perform(.seek(value)) }
    }
    private func cancelDrag() { draggedSlider = nil; dragValue = nil; draggedApplication = nil; draggedAudioID = nil; draggedTrack = nil }
    private var draggedTrackIsCurrent: Bool {
        guard let current = snapshot.track, let original = draggedTrack else { return false }
        return current.hasSameIdentity(as: original)
    }
    private func moveSlider(to point: CGPoint) {
        guard point.x.isFinite, point.y.isFinite, let id = draggedSlider,
              snapshot.application == draggedApplication, let slider = accessibleSliders.first(where: { $0.id == id }), slider.enabled else { return }
        let position = id == "appVolume" ? (slider.rect.maxY - point.y) / slider.rect.height : (point.x - slider.rect.minX) / slider.rect.width
        let fraction = Double(min(1, max(0, position)))
        let value = slider.minimum + fraction * (slider.maximum - slider.minimum)
        dragValue = value
        if id == "seek" { updateProgress(animated: false) }
        else if audioApplication?.id == draggedAudioID { _ = setSlider(id: id, value: value) }
    }
    @discardableResult func setSlider(id: String, value: Double) -> Bool {
        guard active, value.isFinite, let slider = accessibleSliders.first(where: { $0.id == id }), slider.enabled else { return false }
        let bounded = min(slider.maximum, max(slider.minimum, value))
        if id == "seek" { controller.perform(.seek(bounded)); return true }
        guard id == "appVolume" else { return false }
        if controller.playerVolume != nil { return controller.setPlayerVolume(bounded) }
        guard let app = audioApplication else { return false }
        if let session = currentSession {
            if session.state == .failed, bounded == 1 { perAppAudio.stop(processID: app.id); return true }
            return perAppAudio.setGain(bounded, processID: app.id)
        }
        if bounded == 1 { return true }
        guard let output = currentOutput else { return false }
        return perAppAudio.start(application: app, output: output, initialGain: bounded)
    }
    func perform(actionID: String) {
        guard active, accessibleActions.contains(where: { $0.id == actionID && $0.enabled }) else { return }
        cancelDrag()
        if let source = NowPlayingSource(rawValue: actionID) { controller.select(source) }
        else {
            switch actionID {
            case "connect": controller.connect()
            case "playPause": controller.perform(.playPause)
            case "previous": controller.perform(.previous)
            case "next": controller.perform(.next)
            case "volume": showVolume.toggle(); repaint(); onChange?()
            case "lyrics": showLyrics.toggle(); repaint(); reconcileTimer(); onChange?()
            default: return
            }
        }
    }

    private func refresh() {
        guard active else { return }
        if draggedApplication != nil,
           draggedApplication != snapshot.application || (draggedSlider == "seek" && !draggedTrackIsCurrent) { cancelDrag() }
        let identity = "\(snapshot.application?.source.rawValue ?? ""): \(snapshot.track?.title ?? ""):\(snapshot.track?.album ?? "")"
        if identity != trackIdentity { trackIdentity = identity; requestedEndRefresh = false }
        if let duration = snapshot.track?.duration, let elapsed = snapshot.track?.elapsed(at: now()), elapsed < duration - 1 { requestedEndRefresh = false }
        repaint(); reconcileTimer(); onChange?()
    }
    private func reconcileTimer() {
        let current = now()
        guard active, let track = snapshot.track, track.isPlaying,
              let elapsed = track.elapsed(at: current), elapsed.isFinite,
              track.duration.map({ elapsed < $0 }) ?? true else {
            stopClock(); progressFill.removeAllAnimations(); progressHandle.removeAllAnimations(); return
        }
        var delay = floor(elapsed) + 1 - elapsed
        if lyricsVisible, draggedSlider != "seek", let boundary = controller.lyrics?.nextBoundary(after: elapsed) {
            delay = min(delay, boundary - elapsed)
        }
        if let duration = track.duration { delay = min(delay, duration - elapsed) }
        // Pathological millisecond cue clusters coalesce into a bounded local
        // display cadence. Normal cues fire at their exact subsecond deadline.
        delay = max(1.0 / 30.0, delay)
        let deadline = current + delay
        if let existing = clockDeadline, cancelClock != nil, abs(existing - deadline) < 0.0005 { return }
        stopClock()
        clockDeadline = deadline
        let generation = clockGeneration
        cancelClock = scheduleDisplayUpdate(delay) { [weak self] in
            guard let self, self.clockGeneration == generation, self.active else { return }
            self.cancelClock = nil; self.clockDeadline = nil
            self.tick()
        }
    }
    private func stopClock() {
        clockGeneration &+= 1
        cancelClock?(); cancelClock = nil; clockDeadline = nil
    }
    private func tick() {
        guard active else { return }
        updateProgress(animated: true); updateLyrics(); onChange?()
        if !requestedEndRefresh, let track = snapshot.track, let duration = track.duration,
           (track.elapsed(at: now()) ?? 0) >= duration {
            requestedEndRefresh = true; controller.refresh()
        }
        reconcileTimer()
    }
    private func updateProgress(animated: Bool) {
        let value = draggedSlider == "seek" ? dragValue : snapshot.track?.elapsed(at: now())
        let duration = snapshot.track?.duration ?? 0
        let fraction = duration > 0 ? min(1, max(0, (value ?? 0) / duration)) : 0
        let width = seekRect.width * fraction
        let interpolates = animated && draggedSlider == nil && !reduceMotion()
            && snapshot.track?.isPlaying == true && duration > 0
        let destination = interpolates ? min(seekRect.width, width + seekRect.width / duration) : width
        withoutActions {
            // Preview and refreshed metadata take effect immediately. An old
            // interpolation must never keep overriding their model geometry.
            progressFill.removeAnimation(forKey: "nowPlaying.progress")
            progressHandle.removeAnimation(forKey: "nowPlaying.progress")
            progressFill.anchorPoint = CGPoint(x: 0, y: 0.5)
            progressFill.bounds = CGRect(x: 0, y: 0, width: destination, height: 2)
            progressFill.position = CGPoint(x: seekRect.minX, y: seekRect.midY)
            progressHandle.position = CGPoint(x: seekRect.minX + destination, y: seekRect.midY)
            elapsedLabel.string = Self.time(value)
        }
        // Retained compositor interpolation; the timer never queries a player.
        // Its destination is also the model value, so timer tolerance cannot
        // make a completed animation snap backwards to the prior second.
        if interpolates {
            let motion = CABasicAnimation(keyPath: "bounds.size.width")
            motion.fromValue = width; motion.toValue = destination; motion.duration = 1; motion.timingFunction = CAMediaTimingFunction(name: .linear)
            progressFill.add(motion, forKey: "nowPlaying.progress")
            let handle = CABasicAnimation(keyPath: "position.x"); handle.fromValue = seekRect.minX + width; handle.toValue = seekRect.minX + destination
            handle.duration = 1; handle.timingFunction = motion.timingFunction; progressHandle.add(handle, forKey: "nowPlaying.progress")
        }
    }
    func dismissPopover() {
        guard showVolume else { return }
        showVolume = false; repaint(); onChange?()
    }
    private func repaint() {
        let animateLyricsVisibility = rendered && lastLyricsVisible != lyricsVisible && active && !reduceMotion()
        let oldLyricsOpacity = lyricsViewport.presentation()?.opacity ?? lyricsViewport.opacity
        withoutActions {
            panel.frame = CGRect(x: 55, y: 0, width: 330, height: 330)
            panel.backgroundColor = nil; panel.borderColor = nil; panel.borderWidth = 0; panel.cornerRadius = 0
            albumCover.frame = panel.bounds
            albumCover.cornerRadius = 0; albumCover.contentsScale = scale
            if displayedCover !== controller.artworkImage {
                crossfade(albumCover)
                displayedCover = controller.artworkImage; albumCover.contents = displayedCover
            }
            coverPlaceholder.path = CGPath(ellipseIn: CGRect(x: 135, y: 113, width: 60, height: 60), transform: nil)
            coverPlaceholder.fillColor = nil; coverPlaceholder.strokeColor = muted.withAlphaComponent(0.32).cgColor
            coverPlaceholder.lineWidth = 2; coverPlaceholder.isHidden = displayedCover != nil
            // Legibility belongs to the metadata area only; the artwork and
            // surrounding HUD retain their original colors and transparency.
            metadataShade.frame = CGRect(x: 0, y: 250, width: 330, height: 80)
            metadataShade.colors = [NSColor.black.withAlphaComponent(0).cgColor,
                                    NSColor.black.withAlphaComponent(0.76).cgColor, NSColor.black.withAlphaComponent(0.90).cgColor]
            metadataShade.locations = [0, 0.30, 1]; metadataShade.startPoint = CGPoint(x: 0.5, y: 0)
            metadataShade.endPoint = CGPoint(x: 0.5, y: 1); metadataShade.isHidden = snapshot.track == nil
            configureText(titleLabel, CGRect(x: 14, y: 279, width: 302, height: 21), 14, .white)
            titleLabel.font = NSFont.systemFont(ofSize: 14, weight: .semibold)
            replaceMetadata(titleLabel, with: snapshot.track?.title ?? "")
            configureText(artistLabel, CGRect(x: 14, y: 303, width: 302, height: 16), 10, NSColor.white.withAlphaComponent(0.80))
            replaceMetadata(artistLabel, with: snapshot.track?.artist ?? "")
            configureText(emptyLabel, CGRect(x: 20, y: 184, width: 290, height: 23), 13, primary)
            emptyLabel.string = "No music playing"; emptyLabel.alignmentMode = .center
            emptyLabel.isHidden = snapshot.track != nil
            for action in accessibleActions {
                buttonLayers[action.id]?.update(action, dark: dark, accent: accent,
                    selected: (action.id == "lyrics" && showLyrics) || (action.id == "volume" && showVolume),
                    playing: snapshot.track?.isPlaying == true, scale: scale)
            }
            progressRail.frame = CGRect(x: seekRect.minX, y: seekRect.midY - 1, width: seekRect.width, height: 2)
            progressRail.backgroundColor = primary.withAlphaComponent(0.2).cgColor
            progressFill.backgroundColor = accent.cgColor
            progressHandle.bounds = CGRect(x: 0, y: 0, width: 5, height: 8); progressHandle.backgroundColor = primary.cgColor
            progressHandle.opacity = snapshot.track?.duration == nil ? 0 : 1
            configureText(elapsedLabel, CGRect(x: seekRect.minX, y: 390, width: 70, height: 11), 9, muted)
            configureText(durationLabel, CGRect(x: seekRect.maxX - 74, y: 390, width: 74, height: 11), 9, muted)
            durationLabel.string = Self.time(snapshot.track?.duration); durationLabel.alignmentMode = .right
            lyricsViewport.frame = CGRect(x: 55, y: 331, width: 330, height: 48)
            lyricsViewport.isHidden = false; lyricsViewport.opacity = lyricsVisible ? 1 : 0
            for (index, label) in lyricsLabels.enumerated() {
                configureText(label, CGRect(x: 0, y: CGFloat(index) * 16, width: 330, height: 16), 10, primary)
                label.alignmentMode = .center; label.opacity = index == 1 ? 1 : 0.36
            }
            updateLyrics(); updateProgress(animated: false); renderVolume()
        }
        if animateLyricsVisibility {
            let fade = CABasicAnimation(keyPath: "opacity"); fade.fromValue = oldLyricsOpacity; fade.toValue = lyricsViewport.opacity
            fade.duration = 0.24; fade.timingFunction = CAMediaTimingFunction(name: .easeInEaseOut)
            lyricsViewport.add(fade, forKey: "nowPlaying.lyrics.visibility")
        }
        if reduceMotion() { removeAnimations(in: content) }
        lastLyricsVisible = lyricsVisible; rendered = true
    }
    private func updateLyrics() {
        let elapsed = draggedSlider == "seek" ? dragValue : snapshot.track?.elapsed(at: now())
        let lines = controller.lyrics?.window(at: elapsed ?? 0) ?? ["", "", ""]
        let previous = lyricsLabels.map { $0.string as? String ?? "" }
        let animate = active && rendered && lyricsVisible && !reduceMotion() && previous != lines && previous.contains { !$0.isEmpty }
        withoutActions {
            for (index, label) in lyricsLabels.enumerated() {
                let value = lines.indices.contains(index) ? lines[index] : ""
                if label.string as? String != value { label.string = value }
                if animate {
                    let movement = CABasicAnimation(keyPath: "transform.translation.y")
                    movement.fromValue = (elapsed ?? 0) >= (lastLyricPosition ?? 0) ? 7 : -7; movement.toValue = 0
                    let fade = CABasicAnimation(keyPath: "opacity"); fade.fromValue = label.opacity * 0.88; fade.toValue = label.opacity
                    let group = CAAnimationGroup(); group.animations = [movement, fade]; group.duration = 0.20
                    group.timingFunction = CAMediaTimingFunction(name: .easeInEaseOut)
                    label.add(group, forKey: "nowPlaying.lyrics.change")
                } else if !lyricsVisible || reduceMotion() { label.removeAnimation(forKey: "nowPlaying.lyrics.change") }
            }
        }
        lastLyricPosition = elapsed
    }
    private func replaceMetadata(_ label: CATextLayer, with value: String) {
        guard label.string as? String != value else { return }
        if (label.string as? String)?.isEmpty == false { crossfade(label) }
        label.string = value
    }
    private func crossfade(_ item: CALayer) {
        guard active, rendered, !reduceMotion() else { return }
        let transition = CATransition(); transition.type = .fade; transition.duration = 0.20
        transition.timingFunction = CAMediaTimingFunction(name: .easeInEaseOut)
        item.add(transition, forKey: kCATransition)
    }
    private func removeAnimations(in item: CALayer) {
        item.removeAllAnimations(); item.sublayers?.forEach { removeAnimations(in: $0) }
    }
    private func renderVolume() {
        let destination: Float = showVolume ? 1 : 0
        let previous = volumeMenu.presentation()?.opacity ?? volumeMenu.opacity
        let changed = volumeMenu.opacity != destination
        volumeMenu.isHidden = false; volumeMenu.opacity = destination
        if changed, active, !reduceMotion() {
            let fade = CABasicAnimation(keyPath: "opacity"); fade.fromValue = previous; fade.toValue = destination
            fade.duration = 0.16; volumeMenu.add(fade, forKey: "nowPlaying.popover.visibility")
        }
        guard showVolume else { return }
        volumeMenu.frame = volumeBounds
        let rect = volumeMenu.bounds
        let sliderRect = volumeRect.offsetBy(dx: -volumeBounds.minX, dy: -volumeBounds.minY)
        volumeBacking.frame = rect.offsetBy(dx: -3, dy: 4); volumeBacking.backgroundColor = NSColor.black.withAlphaComponent(0.3).cgColor
        volumeFace.frame = rect; volumeFace.backgroundColor = NSColor(white: dark ? 0.08 : 0.92, alpha: 0.98).cgColor
        volumeFace.borderWidth = 0.7; volumeFace.borderColor = accent.withAlphaComponent(0.7).cgColor
        let value = controller.playerVolume ?? currentSession?.gain ?? (volumeAvailable ? 1 : 0)
        volumeRail.frame = CGRect(x: sliderRect.midX - 1, y: sliderRect.minY, width: 2, height: sliderRect.height)
        volumeRail.backgroundColor = primary.withAlphaComponent(0.19).cgColor
        volumeFill.frame = CGRect(x: sliderRect.midX - 1, y: sliderRect.maxY - sliderRect.height * value, width: 2, height: sliderRect.height * value)
        volumeFill.backgroundColor = accent.withAlphaComponent(0.85).cgColor
        volumeHandle.frame = CGRect(x: sliderRect.midX - 4, y: volumeFill.frame.minY - 3.5, width: 8, height: 7)
        volumeHandle.backgroundColor = accent.cgColor; volumeHandle.borderWidth = 0.6; volumeHandle.borderColor = primary.withAlphaComponent(0.65).cgColor
        volumeHandle.opacity = volumeAvailable ? 1 : 0.35
        configureText(volumeLabel, CGRect(x: rect.minX + 3, y: rect.minY + 10, width: rect.width - 6, height: 18), 10, primary)
        volumeLabel.alignmentMode = .center; volumeLabel.string = volumeAvailable ? "\(Int((value * 100).rounded()))%" : "—"
    }
    private func configureText(_ label: CATextLayer, _ rect: CGRect, _ size: CGFloat, _ color: NSColor) {
        label.frame = rect; label.font = NSFont.systemFont(ofSize: size); label.fontSize = size; label.foregroundColor = color.cgColor
        label.contentsScale = HUDRenderScale.contentScale(for: label, baseScale: scale); label.truncationMode = .end
    }
    static func time(_ value: Double?) -> String {
        guard let value, value.isFinite, value >= 0 else { return "—:—" }
        let seconds = Int(min(value, 604_800)); return String(format: "%d:%02d", seconds / 60, seconds % 60)
    }
    private func withoutActions(_ action: () -> Void) { CATransaction.begin(); CATransaction.setDisableActions(true); action(); CATransaction.commit() }
}

/// Fixed layer identities keep transport events from flashing or reallocating
/// the player. Transport uses the original simple rounded faces; menu artwork
/// remains consistent with Personal Card.
private final class NowPlayingButtonArtwork {
    let layer = CALayer()
    private let face = CALayer(), glyph = CAShapeLayer()
    private var feedback: HUDControlHighlightLayer?
    private var previousSize = CGSize.zero
    init() {
        layer.allowsGroupOpacity = false
        layer.addSublayer(face); layer.addSublayer(glyph)
    }
    func update(_ action: VolumeCanvasAction, dark: Bool, accent: NSColor, selected: Bool, playing: Bool, scale: CGFloat) {
        let oldPosition = layer.presentation()?.position ?? layer.position
        let moved = layer.frame != .zero && layer.frame.origin != action.rect.origin
        layer.frame = action.rect; layer.opacity = action.enabled ? 1 : 0.35
        face.frame = layer.bounds; face.cornerRadius = action.id == "playPause" ? action.rect.width / 2 : 3
        face.backgroundColor = (selected ? accent.withAlphaComponent(0.16) : NSColor(white: dark ? 0.96 : 0.10, alpha: 0.07)).cgColor
        face.borderColor = (selected ? accent : NSColor(white: dark ? 0.65 : 0.3, alpha: 0.42)).cgColor
        face.borderWidth = 0
        let path = CGMutablePath(), center = CGPoint(x: action.rect.width / 2, y: action.rect.height / 2)
        glyph.frame = layer.bounds
            if action.id == "lyrics" {
                for row in 0..<3 { path.addRect(CGRect(x: 7, y: 8 + row * 6, width: row == 1 ? 16 : 11, height: 2)) }
            } else if action.id == "volume" {
                path.move(to: CGPoint(x: 6, y: 11)); path.addLine(to: CGPoint(x: 11, y: 11)); path.addLine(to: CGPoint(x: 17, y: 6))
                path.addLine(to: CGPoint(x: 17, y: 24)); path.addLine(to: CGPoint(x: 11, y: 19)); path.addLine(to: CGPoint(x: 6, y: 19)); path.closeSubpath()
                let wave = CGMutablePath(); wave.addArc(center: CGPoint(x: 17, y: 15), radius: 7, startAngle: -.pi / 3, endAngle: .pi / 3, clockwise: false)
                path.addPath(wave.copy(strokingWithWidth: 1.5, lineCap: .round, lineJoin: .round, miterLimit: 1))
            } else if action.id == "playPause" && playing {
                path.addRect(CGRect(x: center.x - 6, y: center.y - 8, width: 4, height: 16)); path.addRect(CGRect(x: center.x + 2, y: center.y - 8, width: 4, height: 16))
            } else {
                let direction: CGFloat = action.id == "previous" ? -1 : 1
                path.move(to: CGPoint(x: center.x - direction * 5, y: center.y - 7))
                path.addLine(to: CGPoint(x: center.x + direction * 7, y: center.y)); path.addLine(to: CGPoint(x: center.x - direction * 5, y: center.y + 7)); path.closeSubpath()
                if action.id != "playPause" { path.addRect(CGRect(x: center.x + direction * 8 - 1, y: center.y - 7, width: 2, height: 14)) }
            }
        glyph.path = path; glyph.fillColor = NSColor(white: dark ? 0.94 : 0.12, alpha: 1).cgColor
        glyph.contentsScale = scale
        if previousSize != layer.bounds.size {
            feedback?.removeFromSuperlayer()
            feedback = HUDControlHighlightLayer.add(to: layer, rect: layer.bounds, shape: action.id == "playPause" ? .ellipse : .rounded, enabled: action.enabled, framed: false)
            previousSize = layer.bounds.size
        }
        feedback?.setEnabled(action.enabled)
        if moved && !HUDRuntimeAppearance.reduceMotion {
            let motion = CABasicAnimation(keyPath: "position"); motion.fromValue = NSValue(point: oldPosition); motion.toValue = NSValue(point: layer.position)
            motion.duration = 0.24; motion.timingFunction = CAMediaTimingFunction(name: .easeInEaseOut); layer.add(motion, forKey: "nowPlaying.control.move")
        }
    }
}
