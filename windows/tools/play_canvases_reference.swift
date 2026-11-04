import AppKit
import QuartzCore

// Actual-source oracle of the redesigned minigame and Now Playing canvases (Mac 1.3.0
// Sources/OrbiPomCanvas.swift with the real OrbiPomSession / OrbiPomRuntime (bundled Matter 0.20.0 +
// orbipom.js on JavaScriptCore, fixed seed), and Sources/NowPlayingCanvas.swift with the real
// NowPlayingController over an in-process scripted NowPlayingBackend, AudioDeviceController(snapshot:)
// and PerAppAudioController.fixture()). Each canvas is built by the unchanged Mac code in scripted states
// and exported as its model layer tree with the design recorder (tools/hud_design_reference_support.swift),
// its page controls and accessibility items and the scenario's INPUT, so the Windows suites rebuild the
// same state. Isolation: temporary CFFIXED_USER_HOME (the best score lives in a private defaults suite
// there); no window, no input posted (points go straight to the canvases), no audio device (snapshot
// controllers), no network (no lyric or artwork fetch: the scripted backend embeds timed lyrics and has
// no cover), Reduce Motion on so every page installs at once and nothing animates.

private var reduce = true
private let env = HUDDesignEnvironment(renderScale: 2, reduceMotion: { reduce })

private func row(_ canvas: HUDDesignCanvas, _ recorder: Recorder, module: String, state: String, language: AppLanguage, light: Bool,
                 input: [String: Any], extra: [String: Any] = [:]) throws -> [String: Any] {
    var row: [String: Any] = ["module": module, "state": state, "language": languageName(language), "light": light,
        "layer": try recorder.tree(canvas.root, id: "root"),
        "controls": canvas.page.controls.map { controlState($0, in: canvas.root) },
        "accessibility": accessibility(canvas.accessibilityItems), "input": input]
    if canvas.showsHeader {
        row["header"] = ["counter": canvas.header.counterTextForTesting, "title": canvas.header.titleTextForTesting.0,
                         "controls": canvas.header.controls.map { controlState($0, in: canvas.root) }]
    }
    if canvas.isMenuOpen || canvas.isPopoverOpen || !(canvas.top.sublayers ?? []).isEmpty { row["top"] = try recorder.tree(canvas.top, id: "top") }
    for (k, v) in extra { row[k] = v }
    return row
}

// MARK: OrbiPom input

private func snapshotJSON(_ s: OrbiPomSnapshot) -> [String: Any] {
    ["state": s.state, "paused": s.paused, "score": s.score, "highScore": s.highScore, "mergeCount": s.mergeCount,
     "skillUseCount": s.skillUseCount, "energy": s.energy, "swapCharge": s.swapCharge, "energyProgress": s.energyProgress,
     "currentLevel": s.currentLevel, "nextLevel": s.nextLevel, "previewX": s.previewX, "previewY": s.previewY,
     "previewSize": s.previewSize, "previewVisible": s.previewVisible, "previewScale": s.previewScale,
     "dangerSeconds": s.dangerSeconds as Any? ?? NSNull(), "skill": s.skill?.rawValue as Any? ?? NSNull(),
     "skillPhase": s.skillPhase as Any? ?? NSNull(), "selectedBodyIDs": s.selectedBodyIDs,
     "hoverBodyID": s.hoverBodyID as Any? ?? NSNull(), "windSurfaceY": s.windSurfaceY as Any? ?? NSNull(),
     "bodies": s.bodies.map { ["id": $0.id, "level": $0.level, "x": $0.x, "y": $0.y, "angle": $0.angle, "size": $0.size,
                               "opacity": $0.opacity, "scale": $0.scale] as [String: Any] },
     "simulationTime": s.simulationTime]
}
private func gameInput(_ canvas: OrbiPomCanvas) -> [String: Any] {
    let session = canvas.session
    return ["snapshot": snapshotJSON(session.snapshot), "bestScore": session.bestScore, "manuallyPaused": session.manuallyPaused,
            "error": session.error as Any? ?? NSNull(), "restartConfirmation": canvas.restartConfirmation,
            "rulesPresented": canvas.rulesPresented, "bodyLayers": canvas.bodyLayerCount]
}
/// Every wrapping text of the open pop-over with the line count HUDText.lines gave it (OrbiPomGuide.Column.height).
private func guideLines(_ layer: CALayer) -> [[String: Any]] {
    var out: [[String: Any]] = []
    func walk(_ l: CALayer) {
        if let t = l as? HUDTextLayer, t.maximumLines == 0, !t.text.isEmpty {
            let style = t.textStyle
            let string = HUDText.attributed(t.runs, language: HUDTextLanguage.tag())
            let lines = HUDText.lines(string, width: t.lineFrame.width, maximumLines: 0, truncate: false).count
            out.append(["text": t.text, "size": style.size, "weight": style.weight.resolved.rawValue, "lineHeight": style.lineHeight,
                        "width": t.lineFrame.width, "lines": lines, "language": HUDTextLanguage.tag()])
        }
        l.sublayers?.forEach(walk)
    }
    walk(layer)
    return out
}

// MARK: Now Playing scripted backend

private final class ScriptedBackend: NowPlayingBackend {
    var track: NowPlayingTrack?
    var playerVolume: Double? = 0.7
    func read(_ app: NowPlayingApplication, cancelled: () -> Bool) throws -> NowPlayingTrack? { track }
    func perform(_ command: NowPlayingCommand, in app: NowPlayingApplication, cancelled: () -> Bool) throws {}
    func requestPermission(for app: NowPlayingApplication, cancelled: () -> Bool) throws {}
    func volume(in app: NowPlayingApplication, cancelled: () -> Bool) throws -> Double? { playerVolume }
    func setVolume(_ value: Double, in app: NowPlayingApplication, cancelled: () -> Bool) throws { playerVolume = value }
}

@main private enum PlayCanvasesReference {
    static func main() throws {
        precondition(CommandLine.arguments.count >= 2)
        let output = URL(fileURLWithPath: CommandLine.arguments[1])
        NSApplication.shared.setActivationPolicy(.prohibited)
        var configuration = AppConfiguration.defaults
        configuration.language = .english; configuration.theme = .dark; configuration.reduceMotion = true
        configuration.accentHex = "FAD41F"
        HUDRuntimeAppearance.configuration = configuration
        L10n.language = .english
        HUDColor.apply(accentHex: "FAD41F", light: false)
        let recorder = try Recorder(output: output)
        var payload: [String: Any] = ["schema": 1]
        let style = HUDModuleContentStyle(dark: true, accent: .yellow, contentsScale: 2)
        func themed(_ light: Bool) { HUDColor.apply(accentHex: "FAD41F", light: light) }

        // MARK: OrbiPom
        let suite = "com.endfield.hud.play-oracle"
        let defaults = UserDefaults(suiteName: suite)!
        defaults.removePersistentDomain(forName: suite)
        defaults.set(71, forKey: OrbiPomSession.bestScoreKey)
        var gameRows: [[String: Any]] = []
        let gameContext = HUDDesignContext(env: env)
        let session = OrbiPomSession(defaults: defaults)
        let game = OrbiPomCanvas(context: gameContext, session: session, foregroundProvider: { true })
        game.automaticallySchedulesFrames = false
        func showGame() { _ = game.makeContent(for: .minigame, style: style) }
        // A deterministic first game: the canvas would start an unseeded one on activation.
        _ = session.start(seed: 12345)
        showGame(); game.setActive(true)
        gameRows.append(try row(game, recorder, module: "minigame", state: "start", language: .english, light: false, input: gameInput(game)))
        // Drops across the box, one per simulated second, through the canvas's own clock path.
        func play(seconds: Int, from start: Int = 0) {
            for second in start..<(start + seconds) {
                guard session.snapshot.isPlaying else { return }
                session.runtime?.movePointer(x: Double(30 + (second * 47) % 160), y: 40)
                _ = session.runtime?.drop()
                for _ in 0..<60 { game.advance(seconds: 1.0 / 60) }
            }
        }
        play(seconds: 8)
        gameRows.append(try row(game, recorder, module: "minigame", state: "playing", language: .english, light: false, input: gameInput(game)))
        game.perform("pause")
        gameRows.append(try row(game, recorder, module: "minigame", state: "paused", language: .english, light: false, input: gameInput(game)))
        game.perform("pause")
        var elapsed = 8
        while !session.snapshot.canUse(.clear) && elapsed < 90 && session.snapshot.isPlaying { play(seconds: 2, from: elapsed); elapsed += 2 }
        var omitted: [String] = []
        if session.snapshot.canUse(.clear) {
            game.perform("clear")
            gameRows.append(try row(game, recorder, module: "minigame", state: "skill", language: .english, light: false, input: gameInput(game)))
            game.perform("clear")
        } else { omitted.append("skill: Eliminate never became usable within 90 simulated seconds") }
        game.perform("rules")
        gameRows.append(try row(game, recorder, module: "minigame", state: "rules", language: .english, light: false, input: gameInput(game),
                                extra: ["guideLines": guideLines(game.top)]))
        game.perform("rules")
        if session.snapshot.isPlaying {
            game.perform("restart")
            gameRows.append(try row(game, recorder, module: "minigame", state: "restartConfirm", language: .english, light: false, input: gameInput(game),
                                    extra: ["overlay": try recorder.tree(gameContext.overlay.layer, id: "overlay")]))
            game.perform("cancelRestart")
        } else { omitted.append("restartConfirm: the game was over before it") }
        L10n.language = .simplifiedChinese; game.setActive(false); showGame(); game.setActive(true)
        gameRows.append(try row(game, recorder, module: "minigame", state: "playing", language: .simplifiedChinese, light: false, input: gameInput(game)))
        game.perform("rules")
        gameRows.append(try row(game, recorder, module: "minigame", state: "rules", language: .simplifiedChinese, light: false, input: gameInput(game),
                                extra: ["guideLines": guideLines(game.top)]))
        game.perform("rules")
        L10n.language = .english; game.setActive(false); showGame(); game.setActive(true)
        // The end of a game: keep dropping at the centre until the original controller ends it.
        var over = false
        for second in 0..<240 where session.snapshot.isPlaying {
            session.runtime?.movePointer(x: 115, y: 40)
            _ = session.runtime?.drop()
            for _ in 0..<60 { game.advance(seconds: 1.0 / 60) }
            if session.snapshot.dangerSeconds != nil, !gameRows.contains(where: { $0["state"] as? String == "danger" }) {
                gameRows.append(try row(game, recorder, module: "minigame", state: "danger", language: .english, light: false, input: gameInput(game)))
            }
            _ = second
        }
        if session.snapshot.state == "over" {
            over = true
            gameRows.append(try row(game, recorder, module: "minigame", state: "over", language: .english, light: false, input: gameInput(game)))
        } else { omitted.append("over: the game had not ended after 240 simulated seconds") }
        _ = over
        game.setActive(false)
        // A fresh game in Light.
        themed(true); gameContext.refreshPalette()
        let lightSession = OrbiPomSession(defaults: defaults)
        let lightGame = OrbiPomCanvas(context: gameContext, session: lightSession, foregroundProvider: { true })
        lightGame.automaticallySchedulesFrames = false
        _ = lightSession.start(seed: 12345)
        _ = lightGame.makeContent(for: .minigame, style: style); lightGame.setActive(true)
        gameRows.append(try row(lightGame, recorder, module: "minigame", state: "start", language: .english, light: true, input: gameInput(lightGame)))
        lightGame.setActive(false)
        themed(false); gameContext.refreshPalette()
        payload["minigame"] = gameRows

        // MARK: Now Playing
        let now: TimeInterval = 1000
        let lyrics = "[00:00.50]Signal Fire\n[00:12]Embers over the plain\n[00:30]Lanterns on the ridge\n[00:41]We carry the light\n[00:48]Through the long night\n[01:05]Signal fire"
        func track(playing: Bool) -> NowPlayingTrack {
            NowPlayingTrack(title: "Signal Fire", artist: "Endfield Ensemble", album: "Talos II", duration: 214, position: 42,
                            isPlaying: playing, sampledAt: now, timedLyrics: lyrics)
        }
        let backend = ScriptedBackend()
        let app = NowPlayingApplication(source: .music, pid: 424_242, bundleURL: URL(fileURLWithPath: "/EndfieldHUD-Oracle/Music.app"))
        let controller = NowPlayingController(backend: backend, applications: { [app] }, subscribe: { _ in {} },
                                              work: { $0() }, deliver: { $0() }, artworkFetch: { _, completion in completion(nil); return {} })
        let audio = AudioDeviceController(snapshot: AudioDeviceSnapshot(outputs: [], inputs: [], defaultOutputID: nil, defaultInputID: nil,
            outputVolume: nil, canSetOutputVolume: false, outputMuted: nil, canSetOutputMute: false, balance: nil, canSetBalance: false,
            canSetDefaultOutput: false, canSetDefaultInput: false, activeApplications: [], availableApplications: [],
            applicationActivitySupported: false, applicationActivityMessage: nil))
        let routes = PerAppAudioController.fixture()
        let playContext = HUDDesignContext(env: env)
        let player = NowPlayingCanvas(context: playContext, controller: controller, audio: audio, perAppAudio: routes,
                                      now: { now }, scheduleDisplayUpdate: { _, _ in {} })
        func playInput(_ state: String) -> [String: Any] {
            let s = controller.snapshot
            var row: [String: Any] = ["now": now, "showsLyrics": player.showsLyrics, "isVolumeOpen": player.isVolumeOpen,
                                      "failure": s.failure != nil, "source": s.application?.source.rawValue as Any? ?? NSNull(),
                                      "playerVolume": controller.playerVolume as Any? ?? NSNull(),
                                      "lyrics": s.track?.timedLyrics as Any? ?? NSNull()]
            if let t = s.track {
                row["track"] = ["title": t.title, "artist": t.artist, "album": t.album, "duration": t.duration as Any? ?? NSNull(),
                                "position": t.position as Any? ?? NSNull(), "isPlaying": t.isPlaying, "sampledAt": t.sampledAt,
                                "supportsSeeking": t.supportsSeeking]
                row["elapsed"] = t.elapsed(at: now) as Any? ?? NSNull()
            } else { row["track"] = NSNull() }
            let play = player.page.controls.first { $0.id == "playPause" }
            row["playLabel"] = play?.label ?? ""
            row["canSeek"] = player.page.controls.first { $0.id == "seek" }?.isEnabled ?? false
            return row
        }
        var playRows: [[String: Any]] = []
        func showPlayer() { _ = player.makeContent(for: .nowPlaying, style: style) }
        func set(_ value: NowPlayingTrack?) { backend.track = value; controller.refresh() }
        backend.track = nil
        player.setPresented(true); showPlayer(); player.setActive(true)
        playRows.append(try row(player, recorder, module: "nowPlaying", state: "nothing", language: .english, light: false, input: playInput("nothing")))
        set(track(playing: true))
        playRows.append(try row(player, recorder, module: "nowPlaying", state: "playing", language: .english, light: false, input: playInput("playing")))
        set(track(playing: false))
        playRows.append(try row(player, recorder, module: "nowPlaying", state: "paused", language: .english, light: false, input: playInput("paused")))
        set(track(playing: true))
        player.perform(actionID: "lyrics")
        playRows.append(try row(player, recorder, module: "nowPlaying", state: "noLyrics", language: .english, light: false, input: playInput("noLyrics")))
        player.perform(actionID: "lyrics")
        player.perform(actionID: "volume")
        playRows.append(try row(player, recorder, module: "nowPlaying", state: "volumeOpen", language: .english, light: false, input: playInput("volumeOpen")))
        player.perform(actionID: "volume")
        // Scrub: press and drag on the seek bar (box px; the timeline is (55, 464) 480 wide, the bar 22 tall).
        _ = player.pointerDown(at: CGPoint(x: 55 + 200, y: 464 + 9))
        player.pointerDragged(to: CGPoint(x: 55 + 300, y: 464 + 9))
        playRows.append(try row(player, recorder, module: "nowPlaying", state: "scrubbing", language: .english, light: false, input: playInput("scrubbing"),
                                extra: ["scrub": ["down": [255, 473], "drag": [355, 473]]]))
        // Not released: deactivating cancels the drag without a seek (NowPlayingSeekBar.cancelDrag),
        // so the scenes below show the track at 42 s again.
        L10n.language = .simplifiedChinese; player.setActive(false); showPlayer(); player.setActive(true)
        playRows.append(try row(player, recorder, module: "nowPlaying", state: "playing", language: .simplifiedChinese, light: false, input: playInput("playing")))
        L10n.language = .english; player.setActive(false); showPlayer(); player.setActive(true)
        themed(true); playContext.refreshPalette(); showPlayer()
        playRows.append(try row(player, recorder, module: "nowPlaying", state: "playing", language: .english, light: true, input: playInput("playing")))
        themed(false); playContext.refreshPalette(); showPlayer()
        player.setActive(false); player.setPresented(false)
        payload["nowPlaying"] = playRows
        payload["omitted"] = omitted

        // Sorted by their keys: dictionary order would make the fixture differ between runs.
        payload["text"] = ["widths": recorder.widths.keys.sorted().map { recorder.widths[$0]! },
                           "metrics": recorder.metrics.keys.sorted().map { recorder.metrics[$0]! }]
        payload["layerDefaults"] = recorder.layerDefaults
        let data = try JSONSerialization.data(withJSONObject: sanitize(payload), options: [.sortedKeys])
        try data.write(to: output.appendingPathComponent("play-canvases-source.json"))
        defaults.removePersistentDomain(forName: suite)
        print("Exported \(gameRows.count) minigame and \(playRows.count) Now Playing scenes; omitted: \(omitted)")
    }
}
