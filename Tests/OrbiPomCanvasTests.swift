import AppKit
import QuartzCore

/// Native retained canvas/input fixtures use one temporary defaults domain and
/// an offscreen host. They never activate the application or run a real window.
enum OrbiPomCanvasTests {
    static func run() -> Int {
        var count = 0
        func check(_ value: Bool, _ message: String) { count += 1; precondition(value, message) }
        func key(_ code: UInt16, text: String = "", repeated: Bool = false,
                 modifiers: NSEvent.ModifierFlags = []) -> NSEvent {
            NSEvent.keyEvent(with: .keyDown, location: .zero, modifierFlags: modifiers,
                timestamp: 0, windowNumber: 0, context: nil, characters: text,
                charactersIgnoringModifiers: text, isARepeat: repeated, keyCode: code)!
        }
        func point(x: CGFloat, y: CGFloat) -> CGPoint {
            CGPoint(x: OrbiPomCanvas.board.minX + x * 1.1, y: OrbiPomCanvas.board.minY + y * 1.1)
        }
        func allLayers(_ parent: CALayer) -> [CALayer] {
            [parent] + (parent.sublayers ?? []).flatMap(allLayers)
        }
        let _ = NSApplication.shared
        let suite = "OrbiPomCanvasTests.\(UUID().uuidString)"
        let defaults = UserDefaults(suiteName: suite)!
        defer { defaults.removePersistentDomain(forName: suite) }
        defaults.set(51, forKey: OrbiPomSession.bestScoreKey)
        let session = OrbiPomSession(defaults: defaults)
        var events: [String] = []; session.onEvent = { events.append($0) }
        var foreground = true
        let canvas = OrbiPomCanvas(session: session, foregroundProvider: { foreground })
        canvas.updateRenderScale(3)
        check(canvas.actions.isEmpty && allLayers(canvas.layer).allSatisfy { $0.contents == nil },
              "An unopened minigame defers control and sprite artwork even when the HUD sets render scale")
        canvas.automaticallySchedulesFrames = false
        let host = NSView(frame: CGRect(x: 0, y: 0, width: 680, height: 600)); host.wantsLayer = true
        let interaction = HUDOrbiPomInteraction(canvas: canvas, host: host)
        defer { interaction.deactivate() }
        let project = CGAffineTransform(a: 1.04, b: 0.13, c: -0.07, d: 0.93, tx: 42, ty: 30)
        interaction.project = { $0.applying(project) }
        interaction.unproject = { $0.applying(project.inverted()) }
        let art = canvas.makeContent(for: .minigame, style: .init(dark: true, accent: .systemPurple, contentsScale: 2))
        host.layer!.addSublayer(art)
        check(art === canvas.layer && art.name == "orbipom.canvas", "Minigame returns one retained canvas on the HUD plane")
        check(session.runtime == nil && !canvas.hasFrameTimer && session.bestScore == 51,
              "Merely constructing hidden minigame artwork neither boots JavaScriptCore nor schedules a clock")
        canvas.advance(seconds: 1); canvas.perform("start")
        check(session.runtime == nil, "Inactive canvas ignores simulation and Start actions")
        interaction.setActive(true)
        check(canvas.isActive && session.runtime == nil && !canvas.hasFrameTimer,
              "Showing the idle Start screen still performs no simulation work")
        func button(_ id: String) -> NSButton? {
            guard let action = canvas.actions.first(where: { $0.id == id }) else { return nil }
            let frame = action.rect.applying(project)
            return host.subviews.compactMap { $0 as? NSButton }.first { !$0.isHidden && $0.frame == frame }
        }
        let startButton = button("start")!
        check(startButton.accessibilityLabel() == canvas.actions.first { $0.id == "start" }!.title
                && startButton.acceptsFirstResponder && startButton.hitTest(.zero) == nil,
              "Start exposes a labeled keyboard-accessible native control while pointer routing stays on the projected canvas")
        check(startButton.accessibilityPerformPress(), "VoiceOver activates the original Start action")
        check(session.snapshot.isPlaying && !session.snapshot.paused && !canvas.hasFrameTimer && events == ["started"],
              "Fixture Start creates one run without the disabled automatic clock")
        check(session.snapshot.highScore == 51, "The new runtime receives only the isolated domain's local best score")
        check(startButton.isHidden, "Start accessibility control retires when gameplay replaces it")
        for action in canvas.actions {
            let b = button(action.id)!
            check(b.frame == action.rect.applying(project) && b.isEnabled == action.enabled,
                  "Every native game action follows its projected rectangle and enabled state")
        }
        check(button("clear")?.accessibilityPerformPress() == false && session.snapshot.skill == nil,
              "Uncharged skill accessibility controls cannot execute")
        check(button("pause")?.accessibilityLabel() == L10n.text("Pause / resume", "暂停 / 继续")
                && button("restart")?.accessibilityLabel() == L10n.text("Restart", "重新开始"),
              "Symbol-only buttons have meaningful localized accessibility names")
        check(canvas.layer.bounds.contains(OrbiPomCanvas.board) && OrbiPomCanvas.playArea.contains(OrbiPomCanvas.board),
              "The full original230×280world and source preview strip fit inside the HUD module")
        let labels = allLayers(canvas.layer).compactMap { ($0 as? CATextLayer)?.string as? String }
        check(labels.contains(L10n.text("SP", "技力")) && !labels.contains(L10n.text("Energy", "能量")),
              "The minigame uses original localized SP terminology")
        let rulesButton = button("rules")!
        let rulesAnchor = canvas.actions.first { $0.id == "rules" }!.rect
        check(rulesButton.accessibilityLabel() == L10n.text("Rules", "游戏规则")
                && rulesAnchor.minX > OrbiPomCanvas.board.maxX && rulesAnchor.minY >= OrbiPomCanvas.board.maxY,
              "Question-mark rules control is at bottom-right with a meaningful accessible name")
        check(rulesButton.accessibilityPerformPress(), "Rules open from the accessible question-mark control")
        let rules = interaction.secondaryMenu!
        check(canvas.rulesPresented && interaction.capturesPointer && session.snapshot.paused && !session.manuallyPaused,
              "Rules temporarily pause the game without changing user pause intent")
        check(rules.artwork.superlayer === canvas.layer && rules.artwork.zPosition > canvas.layer.zPosition
                && rules.artwork.frame.maxY < rulesAnchor.minY,
              "Rules use the existing retained secondary-menu surface anchored above their bottom-right button")
        let menuSource = CGRect(origin:rules.artwork.position,size:rules.contentSize)
        check(rules.frame == menuSource.applying(project)
                && interaction.hitTestMenu(at:CGPoint(x:menuSource.midX,y:menuSource.midY).applying(project)) === rules,
              "Rules bounds and inverse-projected hit testing follow the same tilt as the game")
        check(host.subviews.compactMap { $0 as? NSButton }.allSatisfy(\.isHidden),
              "Game accessibility buttons cannot be invoked through the rules menu")
        let rulesFrozen = session.snapshot
        canvas.advance(seconds:30)
        check(session.snapshot == rulesFrozen && interaction.scroll(delta:24),
              "Rules scrolling consumes input while hidden gameplay remains frozen")
        check(interaction.mouseDown(at:CGPoint(x:5,y:400)) && interaction.secondaryMenu == nil
                && !canvas.rulesPresented && !session.snapshot.paused && session.snapshot.bodies.isEmpty,
              "An outside click closes Rules and resumes only previously running gameplay without dropping")
        if !HUDRuntimeAppearance.reduceMotion {
            check(rules.artwork.animation(forKey:"orbipom.rules") != nil && rules.superview == nil,
                  "Rules detach native input immediately and finish their finite retained closing animation")
        }
        let noBodies = Set(allLayers(canvas.layer).map(ObjectIdentifier.init))
        let target = point(x: 45, y: 65)
        check(interaction.mouseDown(at: target), "Projected interaction accepts an inside sourceworld pointerdown")
        check(session.snapshot.bodies.isEmpty, "Pointerdown aims without prematurely dropping")
        canvas.mouseUp(at: target)
        check(session.snapshot.bodies.filter { $0.id > 0 }.count == 1,
              "Pointerup maps the1.1scale HUD board back to the originalworld and drops exactly one body")
        let first = session.snapshot.bodies.first { $0.id > 0 }!
        check(abs(first.x - 45) < 15 && first.y < 0, "Aimed drop starts at the source's shaped preview offset above the vessel")
        let inserted = allLayers(canvas.layer).filter { !noBodies.contains(ObjectIdentifier($0)) && $0.contents != nil }
        check(inserted.count == 1 && canvas.bodyLayerCount == 1, "One dropped character allocates exactly one retained sprite layer")
        let bodyLayer = inserted[0]
        check(canvas.keyDown(key(49, text: " ")), "Space is handled by the game input path")
        check(session.snapshot.bodies.filter { $0.id > 0 }.count == 1, "Immediate Space respects the original500msdrop cooldown")
        for _ in 0..<31 { canvas.advance(seconds: 1.0 / 60) }
        check(bodyLayer.superlayer != nil && allLayers(canvas.layer).contains { $0 === bodyLayer } && canvas.bodyLayerCount == 1,
              "Physics motion reuses the existing sprite layer and texture")
        let settledCount = session.snapshot.bodies.count
        check(canvas.keyDown(key(49, text: " ", repeated: true)) && session.snapshot.bodies.count == settledCount,
              "Holding Space cannot bypass cooldown or trigger repeated drops")
        let dropBeforeOutside = session.snapshot.bodies.count
        canvas.mouseDown(at: point(x: 110, y: 80)); canvas.mouseUp(at: CGPoint(x: -1, y: -1))
        check(session.snapshot.bodies.count == dropBeforeOutside, "Dragging out of the game before release cancels the drop gesture")
        canvas.mouseUp(at: point(x: 110, y: 80))
        check(session.snapshot.bodies.count == dropBeforeOutside, "An orphan mouseup cannot insert a character")
        let beforeCommand = session.snapshot
        check(!canvas.keyDown(key(49, text: " ", modifiers: .command)) && session.snapshot == beforeCommand,
              "Application command shortcuts are not swallowed by game controls")
        check(canvas.keyDown(key(124)), "Arrow aiming works without a pointer")
        check(canvas.keyDown(key(49, text: " ")) && session.snapshot.bodies.filter { $0.id > 0 }.count == 2,
              "A fresh Space after cooldown drops at the keyboard-adjusted aim")
        canvas.perform("pause")
        check(session.snapshot.paused && !canvas.hasFrameTimer, "Manual Pause freezes the original runtime")
        let dimmer = allLayers(canvas.layer).first { $0.name == "orbipom.boardDimmer" }!
        let status = allLayers(canvas.layer).first { $0.name == "orbipom.status" } as! CATextLayer
        check(dimmer.opacity == 1 && dimmer.frame == OrbiPomCanvas.board && status.string as? String == L10n.text("Paused", "已暂停")
                && abs(status.frame.midX-OrbiPomCanvas.board.midX)<0.01 && abs(status.frame.midY-OrbiPomCanvas.board.midY)<0.01,
              "Manual pause darkens the complete board behind centered legible text")
        canvas.perform("rules")
        check(canvas.rulesPresented && session.manuallyPaused, "Opening Rules preserves an existing manual pause")
        check(interaction.keyDown(key(53)) && !canvas.rulesPresented && session.snapshot.paused && session.manuallyPaused,
              "Closing Rules cannot silently resume a manually paused game")
        interaction.setActive(false); interaction.setActive(true)
        check(session.snapshot.paused && session.manuallyPaused && !canvas.hasFrameTimer,
              "Switching tabs preserves manual pause rather than treating it as a visibility pause")
        interaction.setPresented(false); interaction.setActive(true)
        _ = canvas.makeContent(for:.minigame,style:.init(dark:false,accent:.systemCyan,contentsScale:2))
        check(session.snapshot.paused && session.manuallyPaused && dimmer.opacity == 1,
              "HUD reopen and appearance reconfiguration preserve manual pause and its backdrop")
        interaction.setActive(false)
        autoreleasepool {
            let recreated = OrbiPomCanvas(session:session,foregroundProvider:{ true })
            recreated.automaticallySchedulesFrames=false; recreated.setActive(true)
            check(session.snapshot.paused && session.manuallyPaused && !recreated.hasFrameTimer,
                  "A newly created canvas inherits manual pause from the retained session")
            recreated.deactivate()
        }
        interaction.setActive(true)
        let paused = session.snapshot, pausedSteps = session.runtime!.advanceCount
        for _ in 0..<20 { canvas.advance(seconds: 3) }
        _ = canvas.keyDown(key(49, text: " "))
        check(session.snapshot == paused && session.runtime!.advanceCount == pausedSteps,
              "Paused frames and Space cannot advance timers, physics or the queue")
        _ = canvas.keyDown(key(35, text: "p", repeated: true))
        check(session.snapshot.paused, "Key repeat cannot toggle Pause repeatedly")
        _ = canvas.keyDown(key(35, text: "p")); canvas.advance(seconds: 1.0 / 60)
        check(!session.snapshot.paused && session.snapshot.simulationTime - paused.simulationTime < 0.02,
              "Resume excludes all paused elapsed time")
        canvas.perform("restart")
        check(canvas.restartConfirmation && session.snapshot.paused && button("confirmRestart")?.accessibilityLabel() == L10n.text("Confirm", "确认"),
              "Restart opens the retained projected confirmation and pauses gameplay")
        let originalBodies = session.snapshot.bodies
        check(canvas.keyDown(key(53)) && !canvas.restartConfirmation && !session.snapshot.paused
                && session.snapshot.bodies == originalBodies,
              "Escape cancels restart without resetting the stack")
        session.runtime?.evaluateForTesting("e5().addScore(345)")
        canvas.perform("restart")
        check(session.bestScore == 345 && defaults.integer(forKey: OrbiPomSession.bestScoreKey) == 345,
              "Pausing for restart persists the score only to the isolated defaults suite")
        check(button("confirmRestart")?.accessibilityPerformPress() == true,
              "Confirmation executes through its accessible projected button")
        check(!canvas.restartConfirmation && session.snapshot.bodies.isEmpty && session.snapshot.score == 0
                && session.snapshot.highScore == 345 && canvas.bodyLayerCount == 0 && bodyLayer.superlayer == nil,
              "Confirmed restart clears stale sprite layers and run score while retaining the local best")
        check(events == ["started", "restarted"], "Session publishes one Start/Restart event for each actual run")
        let reloaded = OrbiPomSession(defaults: defaults)
        check(reloaded.bestScore == 345 && reloaded.runtime == nil,
              "A separate session loads the saved best score without starting services")
        for _ in 0..<8 {
            _ = canvas.keyDown(key(49, text: " ")); canvas.advance(seconds: 1.0 / 60)
            check(canvas.bodyLayerCount == session.snapshot.bodies.count, "Sprite layer count stays bounded by live and finite outgoing bodies")
            canvas.perform("restart"); canvas.perform("confirmRestart")
        }
        check(canvas.bodyLayerCount == 0, "Repeated runs leave no retired character layers")
        session.runtime?.evaluateForTesting("e5().addScore(444);var lossTarget=__game.phys.spawn(5,115,0);Matter.Body.setStatic(lossTarget.body,true);")
        for _ in 0..<10 { canvas.advance(seconds: 1.0 / 60) }
        let warning = allLayers(canvas.layer).first { $0.name == "orbipom.dangerCountdown" } as! CATextLayer
        check(!warning.isHidden && warning.frame.minX > OrbiPomCanvas.board.maxX
                && warning.foregroundColor == NSColor.systemRed.cgColor && warning.string as? String == "5",
              "Settled top overflow shows the original countdown in red to the right of the board")
        check(status.isHidden && dimmer.opacity == 0,
              "Danger countdown does not replace centered game text or darken active gameplay")
        for _ in 0..<310 { canvas.advance(seconds: 1.0 / 60) }
        check(session.snapshot.state == "over" && session.bestScore == 444
                && defaults.integer(forKey: OrbiPomSession.bestScoreKey) == 444 && events.filter { $0 == "finished" }.count == 1,
              "Original danger loss reaches the session once and saves its completed score in the isolated suite")
        check(dimmer.opacity == 1 && status.string as? String == L10n.text("Game over", "游戏结束") && warning.isHidden,
              "Game over retires the side countdown and dims the board behind its centered message")
        for _ in 0..<10 { canvas.advance(seconds: 5) }
        check(events.filter { $0 == "finished" }.count == 1 && !canvas.hasFrameTimer,
              "Finished sessions neither duplicate events nor continue a presentation clock")
        canvas.perform("start")
        check(session.snapshot.score == 0 && session.snapshot.highScore == 444 && session.snapshot.isPlaying,
              "Play again starts a clean run while retaining the completed local record")
        let controlsAfterRestarts = host.subviews.count
        for _ in 0..<8 { canvas.perform("pause"); canvas.perform("pause") }
        check(host.subviews.count == controlsAfterRestarts, "Accessibility counterparts reuse action IDs across state changes")
        foreground = false; interaction.setActive(false); interaction.setActive(true)
        check(session.snapshot.paused && !canvas.hasFrameTimer, "Injected inactive-application state prevents the game clock")
        let backgroundSteps = session.runtime!.advanceCount, backgroundState = session.snapshot
        canvas.advance(seconds: 30)
        check(session.runtime!.advanceCount == backgroundSteps && session.snapshot == backgroundState,
              "Background applications perform no physics or visual progress")
        foreground = true; interaction.setActive(false); interaction.setActive(true)
        check(!session.snapshot.paused, "Returning to an active foreground resumes the retained run")
        canvas.automaticallySchedulesFrames = true
        interaction.setActive(false); interaction.setActive(true)
        check(canvas.hasFrameTimer, "A visible active run owns one presentation timer when scheduling is enabled")
        interaction.setPresented(false)
        check(!canvas.hasFrameTimer && session.snapshot.paused && !canvas.isActive,
              "Closing the HUD synchronously invalidates its presentation timer")
        let hiddenState = session.snapshot, hiddenSteps = session.runtime!.advanceCount
        for _ in 0..<30 { canvas.advance(seconds: 60) }
        check(session.snapshot == hiddenState && session.runtime!.advanceCount == hiddenSteps,
              "Closed HUD cannot accumulate or catch up hidden game time")
        check(host.subviews.compactMap { $0 as? NSButton }.allSatisfy(\.isHidden),
              "Closed module hides every keyboard/VoiceOver counterpart")
        check(host.subviews.compactMap { $0 as? NSButton }.allSatisfy { !$0.accessibilityPerformPress() },
              "Retired accessibility controls cannot invoke a hidden game")
        canvas.automaticallySchedulesFrames = false
        interaction.setActive(true)
        check(!canvas.hasFrameTimer && session.snapshot.isPlaying && !session.snapshot.paused,
              "Reopening uses the existing run without allocating another clock")
        canvas.updateRenderScale(1.5)
        check(allLayers(canvas.layer).allSatisfy { $0.contentsScale == 1.5 },
              "Backing-scale updates reach retained game artwork and controls")
        canvas.perform("rules"); let disappearingRules = interaction.secondaryMenu!
        interaction.deactivate()
        check(interaction.secondaryMenu == nil && !interaction.capturesPointer && disappearingRules.artwork.superlayer == nil
                && disappearingRules.superview == nil && !canvas.hasFrameTimer,
              "Hiding the HUD removes rules input/artwork and all temporary menu clock state")
        weak var retiredCanvas: OrbiPomCanvas?
        weak var retiredInteraction: HUDOrbiPomInteraction?
        let retainedSession = OrbiPomSession()
        autoreleasepool {
            var temporaryCanvas: OrbiPomCanvas? = OrbiPomCanvas(session: retainedSession, foregroundProvider: { true })
            temporaryCanvas!.automaticallySchedulesFrames = false
            var temporaryInput: HUDOrbiPomInteraction? = HUDOrbiPomInteraction(canvas: temporaryCanvas!, host: host)
            retiredCanvas = temporaryCanvas; retiredInteraction = temporaryInput
            temporaryInput!.setActive(true); temporaryCanvas!.perform("start")
            temporaryInput = nil; temporaryCanvas = nil
        }
        check(retiredCanvas == nil && retiredInteraction == nil && retainedSession.snapshot.paused,
              "Input callbacks and observers do not retain the canvas or keep its session running after teardown")
        check(defaults.persistentDomain(forName: suite)?.keys.sorted() == [OrbiPomSession.bestScoreKey],
              "Fixture persistence writes only its isolated best-score key")
        return count
    }
}
