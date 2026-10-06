import AppKit

/// Explicit diagnostic entry; the app's --ui-test stores contain no real data.
enum OrbiPomHUDVerification {
    private static var session: Session?
    static func run(overlay: OverlayController) {
        precondition(CommandLine.arguments.contains("--ui-test"))
        session = Session(overlay); session?.start()
    }
    private final class Session {
        let overlay: OverlayController
        var view: SystemHUDView!
        var game: OrbiPomCanvas { view.minigameCanvasForVerification }
        var configuration = AppConfiguration.defaults
        var count = 0, done = false, monitor: Any?
        init(_ overlay: OverlayController) { self.overlay = overlay }
        func start() {
            monitor = NSEvent.addLocalMonitorForEvents(matching:[.mouseMoved,.leftMouseDown,.leftMouseUp,.leftMouseDragged,.scrollWheel,.keyDown,.keyUp]) { _ in nil }
            configuration.language = .english; configuration.closeOnFocusLost = false
            overlay.settingsController!.update { $0 = configuration }; overlay.initialModuleRequest = .minigame
            check(overlay.toggleSystemOverlay(snapshot:.unavailable,configuration:configuration),"Native HUD opens minigame")
            wait("open",until:{ self.overlay.systemPhase == .open }) { [self] in
                view = NSApp.windows.compactMap { $0.contentView as? SystemHUDView }.first!
                check(game.session.runtime == nil && !game.hasFrameTimer,"Opening the idle module does not allocate a VM or game clock")
                game.perform("start")
                wait("active clock",until:{ self.game.hasFrameTimer }) { [self] in
                    check(game.session.snapshot.isPlaying,"Original game starts")
                    let p = CGPoint(x:OrbiPomCanvas.board.midX,y:OrbiPomCanvas.board.minY+100)
                    check(game.mouseDown(at:p),"Game accepts local projected pointer input"); game.mouseUp(at:p)
                    wait("falling piece",until:{ !self.game.session.snapshot.bodies.isEmpty }) { [self] in
                        check(game.bodyLayerCount == game.session.snapshot.bodies.count,"Original bodies use retained artwork")
                        check(game.actions.filter { OrbiPomSkill(rawValue:$0.id) != nil }.count == 4,"All four skill controls share HUD layout")
                        check(overlay.eventLog.events.contains { $0.kind == .minigameAction && $0.metadata == ["action":"started"] },"Game action logs no private data")
                        snapshot(); checkAppearance()
                    }
                }
            }
        }
        func checkAppearance() {
            let runtime = game.session.runtime!
            game.perform("pause")
            check(game.session.manuallyPaused && runtime.snapshot.paused && !game.hasFrameTimer,"Manual pause records session intent")
            later {
                self.snapshot("minigame-paused")
                self.game.perform("rules")
                let input = self.view.minigameInteractionForVerification!
                self.check(input.capturesPointer && self.game.rulesPresented && input.secondaryMenu?.artwork.superlayer === self.game.layer,
                           "Rules use the retained projected menu plane")
                self.later {
                    self.snapshot("minigame-rules")
                    input.secondaryMenu?.perform("close")
                    self.check(self.game.session.manuallyPaused && runtime.snapshot.paused && !self.game.hasFrameTimer,
                               "Closing Rules preserves manual pause")
                    #if !HUD_RELEASE
                    runtime.evaluateForTesting("var diagnosticOverflow=__game.phys.spawn(5,115,0);Matter.Body.setStatic(diagnosticOverflow.body,true);")
                    self.game.perform("pause")
                    self.wait("danger countdown",until:{ runtime.snapshot.dangerSeconds != nil }) {
                        self.snapshot("minigame-danger")
                        self.check(runtime.snapshot.isPlaying,"Top overflow warns before ending the game")
                        self.wait("danger loss",until:{ runtime.snapshot.state == "over" }) {
                            self.later {
                                self.snapshot("minigame-over")
                                self.check(!self.game.hasFrameTimer,"Game-over presentation stops simulation scheduling")
                                self.game.perform("start")
                                let p=CGPoint(x:OrbiPomCanvas.board.midX,y:OrbiPomCanvas.board.minY+100)
                                self.game.mouseDown(at:p); self.game.mouseUp(at:p)
                                self.checkSwitch()
                            }
                        }
                    }
                    #else
                    self.game.perform("pause"); self.checkSwitch()
                    #endif
                }
            }
        }
        func checkSwitch() {
            let runtime = game.session.runtime!
            overlay.selectSystemModule(.archive)
            check(!game.hasFrameTimer && runtime.snapshot.paused,"Switch immediately stops game work during outgoing animation")
            let step = runtime.advanceCount
            wait("archive",until:{ self.overlay.systemSelectedModule == .archive && !self.overlay.isSwitchingSystemModule }) { [self] in
                check(runtime.advanceCount == step,"No simulation runs behind another module")
                overlay.selectSystemModule(.minigame)
                wait("game return",until:{ self.game.isActive && !self.overlay.isSwitchingSystemModule }) { [self] in
                    check(game.session.runtime === runtime,"Switching modules preserves the game session")
                    check(game.hasFrameTimer,"Return resumes the visible game clock")
                    game.perform("pause"); check(!game.hasFrameTimer && runtime.snapshot.paused,"Pause stops frame scheduling")
                    game.perform("pause"); check(game.hasFrameTimer,"Resume starts only one clock")
                    game.perform("restart"); check(game.restartConfirmation && !game.hasFrameTimer,"Restart confirmation pauses play")
                    game.perform("cancelRestart"); check(game.hasFrameTimer && !runtime.snapshot.bodies.isEmpty,"Cancel retains stack")
                    game.perform("pause")
                    overlay.selectSystemModule(.archive)
                    wait("paused archive",until:{ self.overlay.systemSelectedModule == .archive && !self.overlay.isSwitchingSystemModule }) { [self] in
                        overlay.selectSystemModule(.minigame)
                        wait("paused return",until:{ self.game.isActive && !self.overlay.isSwitchingSystemModule }) { [self] in
                            check(game.session.manuallyPaused && runtime.snapshot.paused && !game.hasFrameTimer,
                                  "Manual pause survives leaving and returning to the game tab")
                            view.cancelAnimations()
                            check(!game.hasFrameTimer && !game.isActive,"Cancel animations cannot reactivate a hidden game")
                            overlay.forceCloseSystemOverlay()
                            check(!game.hasFrameTimer,"Forced close stops game clock")
                            reopen(runtime)
                        }
                    }
                }
            }
        }
        func reopen(_ runtime: OrbiPomRuntime) {
            overlay.initialModuleRequest = .minigame
            check(overlay.toggleSystemOverlay(snapshot:.unavailable,configuration:configuration),"HUD reopens")
            wait("reopen",until:{ self.overlay.systemPhase == .open }) { [self] in
                view = NSApp.windows.compactMap { $0.contentView as? SystemHUDView }.first!
                check(game.session.runtime === runtime,"HUD close/reopen retains original simulation")
                check(game.session.manuallyPaused && runtime.snapshot.paused && !game.hasFrameTimer,
                      "Manual pause survives a recreated native HUD canvas")
                game.perform("pause")
                check(game.hasFrameTimer && !runtime.snapshot.paused,"Only explicit Resume restarts a manually paused session")
                overlay.closeSystemOverlay()
                check(!game.hasFrameTimer,"Closing animation starts with game paused")
                wait("closed",until:{ self.overlay.systemPhase == .closed }) { [self] in
                    check(overlay.lastClosedAnimationCount == 0,"Closed HUD leaves no animation tracks")
                    check(overlay.notesForVerification.isEmpty,"Game never writes Notes")
                    done = true; if let monitor { NSEvent.removeMonitor(monitor) }
                    print("PASS: \(count) OrbiPom native assertions; input, timer gating, transition cancellation, session retention and isolation")
                    OrbiPomHUDVerification.session = nil; NSApp.terminate(nil)
                }
            }
        }
        func later(_ action: @escaping () -> Void) {
            DispatchQueue.main.asyncAfter(deadline:.now()+0.22) { [self] in guard !done else { return }; action() }
        }
        func snapshot(_ name: String = "minigame") {
            guard let directory = ProcessInfo.processInfo.environment["HUD_ORBIPOM_PREVIEW_DIR"] else { return }
            do {
                let url = URL(fileURLWithPath:directory); try FileManager.default.createDirectory(at:url,withIntermediateDirectories:true)
                try view.writePNG(to:url.appendingPathComponent(name+".png"),scale:1,background:NSColor(white:0.08,alpha:1).cgColor)
            } catch { check(false,"Snapshot failed: \(error)") }
        }
        func wait(_ label: String,until condition: @escaping () -> Bool,then action: @escaping () -> Void) {
            let deadline = Date().addingTimeInterval(20)
            func poll() {
                guard !done else { return }
                NSApp.windows.compactMap { $0 as? NSPanel }.forEach { $0.ignoresMouseEvents = true }
                if condition() { action(); return }; check(Date() < deadline,"Timed out: " + label,counting:false)
                DispatchQueue.main.asyncAfter(deadline:.now()+0.05,execute:poll)
            }
            DispatchQueue.main.asyncAfter(deadline:.now()+0.05,execute:poll)
        }
        func check(_ value: Bool,_ message: String,counting: Bool = true) {
            if counting { count += 1 }; if !value { done = true; overlay.forceCloseSystemOverlay(); preconditionFailure(message) }
        }
    }
}
